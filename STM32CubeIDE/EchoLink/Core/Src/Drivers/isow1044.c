#include "Drivers/isow1044.h"
#include <stddef.h>


#define ISOW1044_PWRUP_TIME_MS 			(6U)
//#define ISOW1044_DEFAULT_LOCAL_NODE_ID 	(1U)


isow1044_error_t ISOW1044_Init(isow1044_t *dev) {
    if(dev == NULL || dev->hcan == NULL) {
        return ISOW1044_ERR_NULL_PTR;
    }

    ISOW1044_EnableDcdc(dev, dev->dcdc_on);

    HAL_Delay(ISOW1044_PWRUP_TIME_MS);

    /*
    if(ISOW1044_IsFaultActive(dev)) {
        return ISOW1044_ERROR_DEV;
    }
    */

    ISOW1044_SetMode(dev, dev->mode);

    ISOW1044_SetNodeId(dev, dev->local_node_id);

    CAN_FilterTypeDef can_filter 	= {0};
    can_filter.FilterBank           = 0;
    can_filter.FilterMode           = CAN_FILTERMODE_IDMASK;
    can_filter.FilterScale          = CAN_FILTERSCALE_32BIT;
    can_filter.FilterIdHigh         = 0x0000;
    can_filter.FilterIdLow          = 0x0000;
    can_filter.FilterMaskIdHigh     = 0x0000;
    can_filter.FilterMaskIdLow      = 0x0000;
    can_filter.FilterFIFOAssignment = CAN_RX_FIFO0;
    can_filter.FilterActivation     = ENABLE;
    can_filter.SlaveStartFilterBank = 14;

    if(HAL_CAN_ConfigFilter(dev->hcan, &can_filter) != HAL_OK) {
        return ISOW1044_ERR_CAN;
    }

    if(HAL_CAN_Start(dev->hcan) != HAL_OK) {
        return ISOW1044_ERR_CAN;
    }

    HAL_StatusTypeDef hal_err = HAL_CAN_ActivateNotification(dev->hcan, CAN_IT_RX_FIFO0_MSG_PENDING);
    if(hal_err != HAL_OK) {
    	return ISOW1044_ERR_CAN;
    }

    return ISOW1044_OK;
}


void ISOW1044_SetMode(isow1044_t *dev, isow1044_mode_t mode) {
	if(mode >= ISOW1044_MODE_MAX || mode < ISOW1044_MODE_NORMAL) return;

    if(mode == ISOW1044_MODE_NORMAL) {
        HAL_GPIO_WritePin(dev->stb_port, dev->stb_pin, GPIO_PIN_RESET);
    } else {
        HAL_GPIO_WritePin(dev->stb_port, dev->stb_pin, GPIO_PIN_SET);
    }

    HAL_Delay(1);
}


void ISOW1044_EnableDcdc(isow1044_t *dev, bool enable) {
    if(enable) {
        HAL_GPIO_WritePin(dev->en_fnc_port, dev->en_fnc_pin, GPIO_PIN_SET);
    } else {
        HAL_GPIO_WritePin(dev->en_fnc_port, dev->en_fnc_pin, GPIO_PIN_RESET);
    }
}


bool ISOW1044_IsFaultActive(isow1044_t *dev) {
	return (HAL_GPIO_ReadPin(dev->en_fnc_port, dev->en_fnc_pin) == GPIO_PIN_RESET);
}

void ISOW1044_SetNodeId(isow1044_t *dev, uint8_t id) {
	dev->local_node_id = id;
}

isow1044_error_t ISOW1044_Send(isow1044_t *dev, uint32_t std_id, uint8_t *tx_data, uint8_t length) {
	if(tx_data == NULL) {
		return ISOW1044_ERR_NULL_PTR;
	}

	CAN_TxHeaderTypeDef tx_header = {0};
	uint32_t tx_mailbox;

	tx_header.StdId = std_id;
	tx_header.ExtId = 0;
	tx_header.IDE   = CAN_ID_STD;
	tx_header.RTR   = CAN_RTR_DATA;
	tx_header.DLC   = length;
	tx_header.TransmitGlobalTime = DISABLE;

	// Check available mailbox transmissions
	if(HAL_CAN_GetTxMailboxesFreeLevel(dev->hcan) == 0) {
		return ISOW1044_OK; // Busy
	}

	HAL_StatusTypeDef hal_err = HAL_CAN_AddTxMessage(dev->hcan, &tx_header, tx_data, &tx_mailbox);
	if(hal_err != HAL_OK) {
		return ISOW1044_ERR_CAN;
	}

	return ISOW1044_OK;
}

// Callback
bool ISOW1044_ReceiveFrame(CAN_HandleTypeDef *hcan, CAN_RxHeaderTypeDef *rx_header, uint8_t *rx_data) {
    if(HAL_CAN_GetRxFifoFillLevel(hcan, CAN_RX_FIFO0) > 0) {
        if(HAL_CAN_GetRxMessage(hcan, CAN_RX_FIFO0, rx_header, rx_data) == HAL_OK) {
            return true;
        }
    }

    return false;
}
