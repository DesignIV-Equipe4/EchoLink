#ifndef INC_DRIVERS_ISOW1044_H_
#define INC_DRIVERS_ISOW1044_H_

#include <stdbool.h>
#include <stdint.h>
#include "stm32l4xx_hal.h"


typedef enum {
    ISOW1044_MODE_NORMAL	= 0,	// STB = LOW
    ISOW1044_MODE_STANDBY,	  		// STB = HIGH
	ISOW1044_MODE_MAX
} isow1044_mode_t;

typedef enum {
	ISOW1044_ERROR_CAN = 0,
	ISOW1044_ERROR_DEV,
	ISOW1044_ERROR_MAX
} isow1044_error_t;

typedef struct {
    CAN_HandleTypeDef 	*hcan;
    GPIO_TypeDef      	*stb_port;
    uint16_t           	stb_pin;
    GPIO_TypeDef      	*en_flt_port;
    uint16_t           	en_flt_pin;

    uint8_t            	local_node_id;
    isow1044_mode_t		mode;
    bool				dcdc_on;
} isow1044_t;


isow1044_error_t ISOW1044_Init(isow1044_t *dev);
void ISOW1044_SetMode(isow1044_t *dev, isow1044_mode_t mode);
void ISOW1044_EnableDcdc(isow1044_t *dev, bool enable);
bool ISOW1044_IsFaultActive(isow1044_t *dev);

HAL_StatusTypeDef ISOW1044_SendCommand(isow1044_t *dev, uint32_t std_id, uint8_t *tx_data, uint8_t length);

// Callback
bool ISOW1044_ReceiveFrame(CAN_HandleTypeDef *hcan, CAN_RxHeaderTypeDef *rx_header, uint8_t *rx_data);

#endif /* INC_DRIVERS_ISOW1044_H_ */
