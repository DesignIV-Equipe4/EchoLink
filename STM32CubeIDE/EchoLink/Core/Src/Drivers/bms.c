/*
 * bms.c
 *
 *  Created on: 23 nov. 2026
 *      Author: gagno
 */


#include "Drivers/bms.h"
#include <string.h>


static uint8_t BMS_CalcChecksum(const uint8_t *frame) {
    uint8_t sum = 0U;
    for(uint8_t i = 0U; i < (BMS_FRAME_SIZE - 1U); i++) {
        sum += frame[i];
    }

    return sum;
}

static uint16_t BMS_ReadU16BigEndian(const uint8_t *buf) {
    return (uint16_t)(((uint16_t)buf[0] << 8U) | (uint16_t)buf[1]);
}

static uint32_t BMS_ReadU32BigEndian(const uint8_t *buf) {
    return ((uint32_t)buf[0] << 24U) |
           ((uint32_t)buf[1] << 16U) |
           ((uint32_t)buf[2] << 8U)  |
           ((uint32_t)buf[3]);
}

static void BMS_FlushRx(UART_HandleTypeDef *huart) {
    __HAL_UART_CLEAR_OREFLAG(huart);
    __HAL_UART_CLEAR_FEFLAG(huart);
    __HAL_UART_CLEAR_NEFLAG(huart);
    __HAL_UART_FLUSH_DRREGISTER(huart);
}

static bool BMS_ValidateFrame(const uint8_t *rx, bms_cmd_t expected_cmd) {
    if(rx[0] != BMS_START_FLAG) {
        return false;
    }
    if(rx[1] != BMS_RESPONSE_ADDR) {
        return false;
    }
    if(rx[2] != (uint8_t)expected_cmd) {
        return false;
    }
    if(rx[3] != BMS_DATA_LEN) {
        return false;
    }
    if(rx[12] != BMS_CalcChecksum(rx)) {
        return false;
    }

    return true;
}

static HAL_StatusTypeDef BMS_SendCommand(bms_t *bms, bms_cmd_t cmd) {
    uint8_t tx[BMS_FRAME_SIZE] = {0};

    tx[0] = BMS_START_FLAG;
    tx[1] = bms->host_addr;
    tx[2] = (uint8_t)cmd;
    tx[3] = BMS_DATA_LEN;
    // tx[4..11] = 0x00 (Reserved)
    tx[12] = BMS_CalcChecksum(tx);

    BMS_FlushRx(bms->huart);

    return HAL_UART_Transmit(bms->huart, tx, BMS_FRAME_SIZE, bms->timeout_ms);
}

static HAL_StatusTypeDef BMS_TransceiveSingle(bms_t *bms, bms_cmd_t cmd, uint8_t rx_data[8]) {
    uint8_t rx[BMS_FRAME_SIZE] = {0};

    HAL_StatusTypeDef err = BMS_SendCommand(bms, cmd);
    if(err != HAL_OK) {
        return err;
    }

    err = HAL_UART_Receive(bms->huart, rx, BMS_FRAME_SIZE, bms->timeout_ms);
    if(err != HAL_OK) {
        return err;
    }

    if(!BMS_ValidateFrame(rx, cmd)) {
        return HAL_ERROR;
    }

    memcpy(rx_data, &rx[4], BMS_DATA_LEN);

    return HAL_OK;
}


void BMS_Init(bms_t *bms) {
	if((bms == NULL) || (bms->huart == NULL)) {
		return;
	}

    memset(bms, 0, sizeof(bms_t));
    bms->host_addr  = BMS_HOST_ADDR;
    bms->timeout_ms = BMS_UART_TIMEOUT_MS;
}

HAL_StatusTypeDef BMS_GetSocVoltageCurrent(bms_t *bms) {
    uint8_t d[8];
    HAL_StatusTypeDef err = BMS_TransceiveSingle(bms, BMS_CMD_SOC_VOLT_CURR, d);
    if(err != HAL_OK) {
        return err;
    }

    bms->data.cumulative_voltage_v = (float)BMS_ReadU16BigEndian(&d[0]) * 0.1f;
    bms->data.gather_voltage_v     = (float)BMS_ReadU16BigEndian(&d[2]) * 0.1f;
    bms->data.current_a            = ((float)BMS_ReadU16BigEndian(&d[4]) - 30000.0f) * 0.1f;
    bms->data.soc_percent          = (float)BMS_ReadU16BigEndian(&d[6]) * 0.1f;

    return HAL_OK;
}

HAL_StatusTypeDef BMS_GetMinMaxVoltage(bms_t *bms) {
    uint8_t d[8];
    HAL_StatusTypeDef err = BMS_TransceiveSingle(bms, BMS_CMD_MIN_MAX_VOLT, d);
    if(err != HAL_OK) {
        return err;
    }

    bms->data.max_cell_mv = BMS_ReadU16BigEndian(&d[0]);
    bms->data.max_cell_no = d[2];
    bms->data.min_cell_mv = BMS_ReadU16BigEndian(&d[3]);
    bms->data.min_cell_no = d[5];

    return HAL_OK;
}

HAL_StatusTypeDef BMS_GetMinMaxTemp(bms_t *bms) {
    uint8_t d[8];
    HAL_StatusTypeDef err = BMS_TransceiveSingle(bms, BMS_CMD_MIN_MAX_TEMP, d);
    if(err != HAL_OK) {
        return err;
    }

    bms->data.max_temp_c  = (int8_t)((int16_t)d[0] - 40);
    bms->data.max_temp_no = d[1];
    bms->data.min_temp_c  = (int8_t)((int16_t)d[2] - 40);
    bms->data.min_temp_no = d[3];

    return HAL_OK;
}

HAL_StatusTypeDef BMS_GetMosStatus(bms_t *bms) {
    uint8_t d[8];
    HAL_StatusTypeDef err = BMS_TransceiveSingle(bms, BMS_CMD_MOS_STATUS, d);
    if(err != HAL_OK) {
        return err;
    }

    bms->data.charge_state        = (bms_charge_states_t)d[0];
    bms->data.charge_mos_on       = (d[1] != 0U);
    bms->data.discharge_mos_on    = (d[2] != 0U);
    bms->data.bms_life_cycles     = d[3];
    bms->data.remain_capacity_mah = BMS_ReadU32BigEndian(&d[4]);

    return HAL_OK;
}

HAL_StatusTypeDef BMS_GetGenericStatus(bms_t *bms) {
    uint8_t d[8];
    HAL_StatusTypeDef err = BMS_TransceiveSingle(bms, BMS_CMD_GENERIC_STATUS, d);
    if(err != HAL_OK) {
        return err;
    }

    bms->data.cell_count              = (d[0] > BMS_MAX_CELLS) ? BMS_MAX_CELLS : d[0];
    bms->data.temp_sensor_count       = (d[1] > BMS_MAX_TEMP) ? BMS_MAX_TEMP : d[1];
    bms->data.charger_connected       = (d[2] != 0U);
    bms->data.load_connected          = (d[3] != 0U);
    bms->data.io_state                = d[4];
    bms->data.charge_discharge_cycles = BMS_ReadU16BigEndian(&d[5]);

    return HAL_OK;
}

HAL_StatusTypeDef BMS_GetCellVoltages(bms_t *bms) {
    if(bms->data.cell_count == 0U) {
        if(BMS_GetGenericStatus(bms) != HAL_OK || bms->data.cell_count == 0U) {
            return HAL_ERROR;
        }
    }

    uint8_t expected_frames = (uint8_t)((bms->data.cell_count + 2U) / 3U);
    if(expected_frames > 16U) {
        expected_frames = 16U;
    }

    HAL_StatusTypeDef err = BMS_SendCommand(bms, BMS_CMD_CELL_VOLTAGES);
    if(err != HAL_OK) {
        return err;
    }

    for(uint8_t f = 0U; f < expected_frames; f++) {
        uint8_t rx[BMS_FRAME_SIZE] = {0};
        err = HAL_UART_Receive(bms->huart, rx, BMS_FRAME_SIZE, bms->timeout_ms);
        if(err != HAL_OK) {
            return err;
        }
        if(!BMS_ValidateFrame(rx, BMS_CMD_CELL_VOLTAGES)) {
            return HAL_ERROR;
        }

        uint8_t frame_num = rx[4];
        if(frame_num == 0xFFU) {
            break; // Invalid frame
        }

        // TODO: Check numerotation (maybe add frame_num - 1U)
        uint8_t base_idx = (uint8_t)(frame_num * 3U);
        for(uint8_t i = 0U; i < 3U; i++) {
            uint8_t cell_idx = base_idx + i;
            if(cell_idx < bms->data.cell_count) {
                bms->data.cell_voltages_mv[cell_idx] = BMS_ReadU16BigEndian(&rx[5U + (i * 2U)]);
            }
        }
    }

    return HAL_OK;
}

HAL_StatusTypeDef BMS_GetCellTemps(bms_t *bms) {
    if(bms->data.temp_sensor_count == 0U) {
        if(BMS_GetGenericStatus(bms) != HAL_OK || bms->data.temp_sensor_count == 0U) {
            return HAL_ERROR;
        }
    }

    uint8_t expected_frames = (uint8_t)((bms->data.temp_sensor_count + 6U) / 7U);
    if(expected_frames > 3U) {
        expected_frames = 3U;
    }

    HAL_StatusTypeDef err = BMS_SendCommand(bms, BMS_CMD_CELL_TEMPS);
    if(err != HAL_OK) {
        return err;
    }

    for(uint8_t f = 0U; f < expected_frames; f++) {
        uint8_t rx[BMS_FRAME_SIZE] = {0};
        err = HAL_UART_Receive(bms->huart, rx, BMS_FRAME_SIZE, bms->timeout_ms);
        if(err != HAL_OK) {
            return err;
        }
        if(!BMS_ValidateFrame(rx, BMS_CMD_CELL_TEMPS)) {
            return HAL_ERROR;
        }

        uint8_t frame_num = rx[4];
        if(frame_num == 0xFFU) {
            break;
        }

        uint8_t base_idx = (uint8_t)(frame_num * 7U);
        for(uint8_t i = 0U; i < 7U; i++) {
            uint8_t temp_idx = base_idx + i;
            if(temp_idx < bms->data.temp_sensor_count) {
                bms->data.cell_temps_c[temp_idx] = (int8_t)((int16_t)rx[5U + i] - 40);
            }
        }
    }

    return HAL_OK;
}

HAL_StatusTypeDef BMS_GetCellBalance(bms_t *bms) {
    uint8_t d[8];
    HAL_StatusTypeDef err = BMS_TransceiveSingle(bms, BMS_CMD_CELL_BALANCE, d);
    if(err != HAL_OK) {
        return err;
    }

    uint64_t mask = 0ULL;
    for(uint8_t i = 0U; i < 6U; i++) {
        mask |= ((uint64_t)d[i] << (i * 8U));
    }
    bms->data.cell_balance_mask = mask;

    return HAL_OK;
}

HAL_StatusTypeDef BMS_GetFailureStatus(bms_t *bms) {
    uint8_t d[8];
    HAL_StatusTypeDef err = BMS_TransceiveSingle(bms, BMS_CMD_FALURE_STATUS, d);
    if(err != HAL_OK) {
        return err;
    }

    bms->data.faults.volt_fault     	= d[0];
    bms->data.faults.temp_fault     	= d[1];
    bms->data.faults.current_soc_fault 	= d[2];
    bms->data.faults.diff_fault     	= d[3];
    bms->data.faults.mos_fault      	= d[4];
    bms->data.faults.sys_fault      	= d[5];
    bms->data.faults.protection_fault   = d[6];
    bms->data.faults.fault_code         = d[7];

    return HAL_OK;
}

HAL_StatusTypeDef BMS_UpdateAll(bms_t *bms) {
    if(BMS_GetGenericStatus(bms) != HAL_OK)		return HAL_ERROR;
    if(BMS_GetSocVoltageCurrent(bms) != HAL_OK) return HAL_ERROR;
    if(BMS_GetMinMaxVoltage(bms) != HAL_OK)     return HAL_ERROR;
    if(BMS_GetMinMaxTemp(bms) != HAL_OK)        return HAL_ERROR;
    if(BMS_GetMosStatus(bms) != HAL_OK)         return HAL_ERROR;
    if(BMS_GetCellVoltages(bms) != HAL_OK)      return HAL_ERROR;
    if(BMS_GetCellTemps(bms) != HAL_OK)         return HAL_ERROR;
    if(BMS_GetCellBalance(bms) != HAL_OK)       return HAL_ERROR;
    if(BMS_GetFailureStatus(bms) != HAL_OK)     return HAL_ERROR;

    return HAL_OK;
}

bool BMS_IsCellBalancing(const bms_t *bms, uint8_t cell_index_1_to_48) {
    if(cell_index_1_to_48 < 1U || cell_index_1_to_48 > BMS_MAX_CELLS) {
        return false;
    }

    return ((bms->data.cell_balance_mask >> (cell_index_1_to_48 - 1U)) & 1ULL) != 0ULL;
}
