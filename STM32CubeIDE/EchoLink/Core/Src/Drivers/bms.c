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

static bms_error_t BMS_MapStatus(HAL_StatusTypeDef hal_error, bool is_tx) {
    switch (hal_error) {
        case HAL_OK:
            return BMS_OK;
        case HAL_TIMEOUT:
            return BMS_ERR_UART_TIMEOUT;
        case HAL_BUSY:
            return BMS_ERR_UART_BUSY;
        case HAL_ERROR:
        default:
            return is_tx ? BMS_ERR_UART_TX : BMS_ERR_UART_RX;
    }
}

static void BMS_FlushRx(UART_HandleTypeDef *huart) {
    __HAL_UART_CLEAR_OREFLAG(huart);
    __HAL_UART_CLEAR_FEFLAG(huart);
    __HAL_UART_CLEAR_NEFLAG(huart);
    __HAL_UART_FLUSH_DRREGISTER(huart);
}

static bms_error_t BMS_ValidateFrame(const uint8_t *rx, bms_cmd_t expected_cmd) {
	if((expected_cmd >= BMS_CMD_MAX) || (expected_cmd < BMS_CMD_SOC_VOLT_CURR)) {
		return BMS_ERR_INVALID_PARAM;
	}

    if(rx[0] != BMS_START_FLAG) {
        return BMS_ERR_INVALID_START;
    }
    if(rx[1] != BMS_RESPONSE_ADDR) {
        return BMS_ERR_INVALID_ADDR;
    }
    if(rx[2] != (uint8_t)expected_cmd) {
        return BMS_ERR_INVALID_CMD;
    }
    if(rx[3] != BMS_DATA_LEN) {
        return BMS_ERR_INVALID_LEN;
    }
    if(rx[12] != BMS_CalcChecksum(rx)) {
        return BMS_ERR_CHECKSUM;
    }

    return BMS_OK;
}

static bms_error_t BMS_SendCommand(bms_t *bms, bms_cmd_t cmd) {
	if((bms == NULL) || (bms->huart == NULL)) {
		return BMS_ERR_INVALID_PARAM;
	}
	if((cmd >= BMS_CMD_MAX) || (cmd < BMS_CMD_SOC_VOLT_CURR)) {
		return BMS_ERR_INVALID_PARAM;
	}

    uint8_t tx[BMS_FRAME_SIZE] = {0};
    tx[0] = BMS_START_FLAG;
    tx[1] = bms->host_addr;
    tx[2] = (uint8_t)cmd;
    tx[3] = BMS_DATA_LEN;
    // tx[4..11] = 0x00 (Reserved)
    tx[12] = BMS_CalcChecksum(tx);

    BMS_FlushRx(bms->huart);
    HAL_StatusTypeDef hal_err = HAL_UART_Transmit(bms->huart, tx, BMS_FRAME_SIZE, bms->timeout_ms);

    return BMS_MapStatus(hal_err, true);
}

static bms_error_t BMS_TransceiveSingle(bms_t *bms, bms_cmd_t cmd, uint8_t rx_data[8]) {
	if((bms == NULL) || (bms->huart == NULL) || (rx_data == NULL)) {
		return BMS_ERR_NULL_PTR;
	}
	if((cmd >= BMS_CMD_MAX) || (cmd < BMS_CMD_SOC_VOLT_CURR)) {
		return BMS_ERR_INVALID_PARAM;
	}

	bms_error_t err = BMS_SendCommand(bms, cmd);
    if(err != BMS_OK) {
        return err;
    }

    uint8_t rx[BMS_FRAME_SIZE] = {0};
    HAL_StatusTypeDef hal_err = HAL_UART_Receive(bms->huart, rx, BMS_FRAME_SIZE, bms->timeout_ms);
    if(hal_err != HAL_OK) {
        return BMS_MapStatus(hal_err, false);
    }

    err = BMS_ValidateFrame(rx, cmd);
    if(err != BMS_OK) {
		return err;
	}

    memcpy(rx_data, &rx[4], BMS_DATA_LEN);

    return BMS_OK;
}


bms_error_t BMS_Init(bms_t *bms) {
	if((bms == NULL) || (bms->huart == NULL)) {
		return BMS_ERR_NULL_PTR;
	}

    memset(bms, 0, sizeof(bms_t));
    bms->host_addr  = BMS_HOST_ADDR;
    bms->timeout_ms = BMS_UART_TIMEOUT_MS;

    return BMS_OK;
}

bms_error_t BMS_GetSocVoltageCurrent(bms_t *bms) {
    uint8_t d[8];
    bms_error_t err = BMS_TransceiveSingle(bms, BMS_CMD_SOC_VOLT_CURR, d);
    if(err != BMS_OK) {
        return err;
    }

    bms->data.cumulative_voltage_v = (float)BMS_ReadU16BigEndian(&d[0]) * 0.1f;
    bms->data.gather_voltage_v     = (float)BMS_ReadU16BigEndian(&d[2]) * 0.1f;
    bms->data.current_a            = ((float)BMS_ReadU16BigEndian(&d[4]) - 30000.0f) * 0.1f;
    bms->data.soc_percent          = (float)BMS_ReadU16BigEndian(&d[6]) * 0.1f;

    return BMS_OK;
}

bms_error_t BMS_GetMinMaxVoltage(bms_t *bms) {
    uint8_t d[8];
    bms_error_t err = BMS_TransceiveSingle(bms, BMS_CMD_MIN_MAX_VOLT, d);
    if(err != BMS_OK) {
        return err;
    }

    bms->data.max_cell_mv = BMS_ReadU16BigEndian(&d[0]);
    bms->data.max_cell_no = d[2];
    bms->data.min_cell_mv = BMS_ReadU16BigEndian(&d[3]);
    bms->data.min_cell_no = d[5];

    return BMS_OK;
}

bms_error_t BMS_GetMinMaxTemp(bms_t *bms) {
    uint8_t d[8];
    bms_error_t err = BMS_TransceiveSingle(bms, BMS_CMD_MIN_MAX_TEMP, d);
    if(err != BMS_OK) {
        return err;
    }

    bms->data.max_temp_c  = (int8_t)((int16_t)d[0] - 40);
    bms->data.max_temp_no = d[1];
    bms->data.min_temp_c  = (int8_t)((int16_t)d[2] - 40);
    bms->data.min_temp_no = d[3];

    return BMS_OK;
}

bms_error_t BMS_GetMosStatus(bms_t *bms) {
    uint8_t d[8];
    bms_error_t err = BMS_TransceiveSingle(bms, BMS_CMD_MOS_STATUS, d);
    if(err != BMS_OK) {
        return err;
    }

    bms->data.charge_state        = (bms_charge_states_t)d[0];
    bms->data.charge_mos_on       = (d[1] != 0U);
    bms->data.discharge_mos_on    = (d[2] != 0U);
    bms->data.bms_life_cycles     = d[3];
    bms->data.remain_capacity_mah = BMS_ReadU32BigEndian(&d[4]);

    return BMS_OK;
}

bms_error_t BMS_GetGenericStatus(bms_t *bms) {
    uint8_t d[8];
    bms_error_t err = BMS_TransceiveSingle(bms, BMS_CMD_GENERIC_STATUS, d);
    if(err != BMS_OK) {
        return err;
    }

    bms->data.cell_count              = (d[0] > BMS_MAX_CELLS_COUNT) ? BMS_MAX_CELLS_COUNT : d[0];
    bms->data.temp_sensor_count       = (d[1] > BMS_MAX_TEMP_COUNT) ? BMS_MAX_TEMP_COUNT : d[1];
    bms->data.charger_connected       = (d[2] != 0U);
    bms->data.load_connected          = (d[3] != 0U);
    bms->data.io_state                = d[4];
    bms->data.charge_discharge_cycles = BMS_ReadU16BigEndian(&d[5]);

    return BMS_OK;
}

bms_error_t BMS_GetCellVoltages(bms_t *bms) {
    if(bms->data.cell_count == 0U) {
    	bms_error_t err = BMS_GetGenericStatus(bms);
		if(err != BMS_OK) {
			return err;
		}
		if(bms->data.cell_count == 0U) {
			return BMS_ERR_NO_CONFIG;
		}
    }

    uint8_t expected_frames = (uint8_t)((bms->data.cell_count + 2U) / 3U);
    if(expected_frames > 16U) {
        expected_frames = 16U;
    }

    bms_error_t err = BMS_SendCommand(bms, BMS_CMD_CELL_VOLTAGES);
    if(err != BMS_OK) {
        return err;
    }

    for(uint8_t f = 0U; f < expected_frames; f++) {
        uint8_t rx[BMS_FRAME_SIZE] = {0};
        HAL_StatusTypeDef hal_err = HAL_UART_Receive(bms->huart, rx, BMS_FRAME_SIZE, bms->timeout_ms);
        if(hal_err != HAL_OK) {
            return BMS_MapStatus(hal_err, false);
        }
        bms_error_t err = BMS_ValidateFrame(rx, BMS_CMD_CELL_VOLTAGES);
        if(err != BMS_OK) {
            return err;
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

    return BMS_OK;
}

bms_error_t BMS_GetCellTemps(bms_t *bms) {
	if(bms->data.temp_sensor_count == 0U) {
		bms_error_t err = BMS_GetGenericStatus(bms);
		if(err != BMS_OK) {
			return err;
		}
		if(bms->data.temp_sensor_count == 0U) {
			return BMS_ERR_NO_CONFIG;
		}
	}

    uint8_t expected_frames = (uint8_t)((bms->data.temp_sensor_count + 6U) / 7U);
    if(expected_frames > 3U) {
        expected_frames = 3U;
    }

    bms_error_t err = BMS_SendCommand(bms, BMS_CMD_CELL_TEMPS);
    if(err != BMS_OK) {
        return err;
    }

    for(uint8_t f = 0U; f < expected_frames; f++) {
        uint8_t rx[BMS_FRAME_SIZE] = {0};
        HAL_StatusTypeDef hal_err = HAL_UART_Receive(bms->huart, rx, BMS_FRAME_SIZE, bms->timeout_ms);
        if(hal_err != HAL_OK) {
            return BMS_MapStatus(hal_err, false);
        }
        err = BMS_ValidateFrame(rx, BMS_CMD_CELL_TEMPS);
        if(err != BMS_OK) {
            return err;
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

    return BMS_OK;
}

bms_error_t BMS_GetCellBalance(bms_t *bms) {
    uint8_t d[8];
    bms_error_t err = BMS_TransceiveSingle(bms, BMS_CMD_CELL_BALANCE, d);
    if(err != BMS_OK) {
        return err;
    }

    uint64_t mask = 0ULL;
    for(uint8_t i = 0U; i < 6U; i++) {
        mask |= ((uint64_t)d[i] << (i * 8U));
    }
    bms->data.cell_balance_mask = mask;

    return BMS_OK;
}

bms_error_t BMS_GetFailureStatus(bms_t *bms) {
    uint8_t d[8];
    bms_error_t err = BMS_TransceiveSingle(bms, BMS_CMD_FALURE_STATUS, d);
    if(err != BMS_OK) {
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

    return BMS_OK;
}

// TODO: add errors listing instead of simply return BMS_OK
bms_error_t BMS_UpdateAll(bms_t *bms) {
    if(BMS_GetGenericStatus(bms) != BMS_OK)		return BMS_OK;
    if(BMS_GetSocVoltageCurrent(bms) != BMS_OK) return BMS_OK;
    if(BMS_GetMinMaxVoltage(bms) != BMS_OK)     return BMS_OK;
    if(BMS_GetMinMaxTemp(bms) != BMS_OK)        return BMS_OK;
    if(BMS_GetMosStatus(bms) != BMS_OK)         return BMS_OK;
    if(BMS_GetCellVoltages(bms) != BMS_OK)      return BMS_OK;
    if(BMS_GetCellTemps(bms) != BMS_OK)         return BMS_OK;
    if(BMS_GetCellBalance(bms) != BMS_OK)       return BMS_OK;
    if(BMS_GetFailureStatus(bms) != BMS_OK)     return BMS_OK;

    return BMS_OK;
}

bool BMS_IsCellBalancing(const bms_t *bms, uint8_t cell_index_1_to_48) {
    if(cell_index_1_to_48 < 1U || cell_index_1_to_48 > BMS_MAX_CELLS_COUNT) {
        return false;
    }

    return ((bms->data.cell_balance_mask >> (cell_index_1_to_48 - 1U)) & 1ULL) != 0ULL;
}
