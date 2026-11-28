/*
 * bms.h
 *
 *  Created on: 23 nov. 2026
 *      Author: gagno
 */

#ifndef INC_DRIVERS_BMS_H_
#define INC_DRIVERS_BMS_H_


#include "stm32l4xx_hal.h"
#include <stdint.h>
#include <stdbool.h>


#define BMS_START_FLAG 			0xA5U
#define BMS_RESPONSE_ADDR     	0x01U

#define BMS_HOST_ADDR          	0x40U // or 0x80

#define BMS_DATA_LEN        	0x08U
#define BMS_FRAME_SIZE        	13U
#define BMS_UART_TIMEOUT_MS    	150U

#define BMS_MAX_CELLS_COUNT   	48U
#define BMS_MAX_TEMP_COUNT     	16U

#define BMS_MIN_TEMP_SENSOR_C	(-40)
#define BMS_MAX_TEMP_SENSOR_C	(85)


typedef enum {
    BMS_OK = 0,
	BMS_ERR_NULL_PTR,
    BMS_ERR_INVALID_PARAM,
    BMS_ERR_UART_TX,
    BMS_ERR_UART_RX,
    BMS_ERR_UART_TIMEOUT,
    BMS_ERR_UART_BUSY,
    BMS_ERR_INVALID_START,
    BMS_ERR_INVALID_ADDR,
    BMS_ERR_INVALID_CMD,
    BMS_ERR_INVALID_LEN,
    BMS_ERR_CHECKSUM,
    BMS_ERR_INVALID_FRAME_IDX,
    BMS_ERR_NO_CONFIG
} bms_error_t;

typedef enum {
    BMS_CMD_SOC_VOLT_CURR  	= 0x90U,	// SOC, voltage and current
    BMS_CMD_MIN_MAX_VOLT   	= 0x91U,	// Min/Max voltage
    BMS_CMD_MIN_MAX_TEMP   	= 0x92U,	// Min/Max temperature
    BMS_CMD_MOS_STATUS     	= 0x93U,    // Charge/discharge state, MOS, capacity
    BMS_CMD_GENERIC_STATUS 	= 0x94U,    // Number of cells, sensors, charger, I/O
    BMS_CMD_CELL_VOLTAGES  	= 0x95U,    // Individual cell voltages (1-48)
    BMS_CMD_CELL_TEMPS     	= 0x96U,    // Individual cell temperatures (1-16)
    BMS_CMD_CELL_BALANCE   	= 0x97U,    // Individual cell balancing (1-48)
    BMS_CMD_FALURE_STATUS 	= 0x98U,   	// Fault flags
	BMS_CMD_MAX
} bms_cmd_t;

typedef enum {
    BMS_STATE_IDLE = 0U,
    BMS_STATE_CHARGING,
    BMS_STATE_DISCHARGING,
	BMS_STATE_MAX
} bms_charge_states_t;

typedef struct {
    uint8_t volt_fault;      	// Under/overvoltage -> byte0
    uint8_t temp_fault;      	// Under/overtemperature -> byte1
    uint8_t current_soc_fault;  // Overcurrent and under/oversoc -> byte2
    uint8_t diff_fault;      	// Diff voltage-temperature -> byte3
    uint8_t mos_fault;       	// MOS/sensors fault -> byte4
    uint8_t sys_fault; 			// AFE, EEPROM, RTC, precharging, comm  -> byte5
    uint8_t protection_fault;	// CC  -> byte6
    uint8_t fault_code;			// Default code (associtated to fault)
} bms_faults_t;

typedef struct {
    // 0x90
    float 				cumulative_voltage_v;   	// 0.1V
    float 				gather_voltage_v;       	// 0.1V
    float 				current_a;              	// 0.1A (offset 30000)
    float 				soc_percent;         		// 0.1%
    // 0x91
    uint16_t 			max_cell_mv;
    uint8_t 			max_cell_no;
    uint16_t 			min_cell_mv;
    uint8_t  			min_cell_no;
    // 0x92
    int16_t   			max_temp_c;            		// offset 40deg
    uint8_t  			max_temp_no;
    int16_t   			min_temp_c;            		// offset 40deg
    uint8_t  			min_temp_no;
    // 0x93
    bms_charge_states_t charge_state;
    bool     			charge_mos_on;
    bool     			discharge_mos_on;
    uint8_t  			bms_life_cycles;	       	// on 255 scale
    uint32_t 			remain_capacity_mah;
    // 0x94
    uint8_t  			cell_count;
    uint8_t  			temp_sensor_count;
    bool     			charger_connected;
    bool     			load_connected;
    uint8_t  			io_state;              		// Bits 0..3: DI1..4, Bits 4..7: DO1..4
    uint16_t 			charge_discharge_cycles;	// Check if not reserved
    // 0x95
    uint16_t 			cell_voltages_mv[BMS_MAX_CELLS_COUNT];
    // 0x96
    int16_t   			cell_temps_c[BMS_MAX_TEMP_COUNT];
    // 0x97
    uint64_t 			cell_balance_mask;
    // 0x98
    bms_faults_t 		faults;
} bms_data_t;

typedef struct {
    UART_HandleTypeDef 	*huart;
    uint8_t 			host_addr;
    uint32_t 			timeout_ms;
    bms_data_t 			data;
} bms_t;


bms_error_t BMS_Init(bms_t *bms);

bms_error_t BMS_GetSocVoltageCurrent(bms_t *bms);
bms_error_t BMS_GetMinMaxVoltage(bms_t *bms);
bms_error_t BMS_GetMinMaxTemp(bms_t *bms);
bms_error_t BMS_GetMosStatus(bms_t *bms);
bms_error_t BMS_GetGenericStatus(bms_t *bms);
bms_error_t BMS_GetCellVoltages(bms_t *bms);
bms_error_t BMS_GetCellTemps(bms_t *bms);
bms_error_t BMS_GetCellBalance(bms_t *bms);
bms_error_t BMS_GetFailureStatus(bms_t *bms);

bms_error_t BMS_UpdateAll(bms_t *bms);
bool BMS_IsCellBalancing(const bms_t *bms, uint8_t cell_index_1_to_48);


#endif /* INC_DRIVERS_BMS_H_ */
