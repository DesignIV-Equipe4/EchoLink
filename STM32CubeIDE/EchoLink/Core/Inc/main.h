/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32l4xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define ISO_STB_Pin GPIO_PIN_13
#define ISO_STB_GPIO_Port GPIOC
#define ISO_FLT_Pin GPIO_PIN_14
#define ISO_FLT_GPIO_Port GPIOC
#define EXTEND_IO1_Pin GPIO_PIN_15
#define EXTEND_IO1_GPIO_Port GPIOC
#define EXTEND_IO2_Pin GPIO_PIN_0
#define EXTEND_IO2_GPIO_Port GPIOH
#define EXTEND_IO3_Pin GPIO_PIN_1
#define EXTEND_IO3_GPIO_Port GPIOH
#define SYS_FNC_Pin GPIO_PIN_0
#define SYS_FNC_GPIO_Port GPIOA
#define MOD_OPT_Pin GPIO_PIN_1
#define MOD_OPT_GPIO_Port GPIOA
#define LORA_INT_Pin GPIO_PIN_2
#define LORA_INT_GPIO_Port GPIOA
#define LORA_nRST_Pin GPIO_PIN_3
#define LORA_nRST_GPIO_Port GPIOA
#define LORA_SPI1_NSS_Pin GPIO_PIN_4
#define LORA_SPI1_NSS_GPIO_Port GPIOA
#define LORA_SPI1_SCK_Pin GPIO_PIN_5
#define LORA_SPI1_SCK_GPIO_Port GPIOA
#define LORA_SPI1_MISO_Pin GPIO_PIN_6
#define LORA_SPI1_MISO_GPIO_Port GPIOA
#define LORA_SPI1_MOSI_Pin GPIO_PIN_7
#define LORA_SPI1_MOSI_GPIO_Port GPIOA
#define LORA_RXEN_Pin GPIO_PIN_0
#define LORA_RXEN_GPIO_Port GPIOB
#define LORA_BUSY_Pin GPIO_PIN_1
#define LORA_BUSY_GPIO_Port GPIOB
#define BMS_LPUART1_RX_Pin GPIO_PIN_10
#define BMS_LPUART1_RX_GPIO_Port GPIOB
#define BMS_LPUART1_TX_Pin GPIO_PIN_11
#define BMS_LPUART1_TX_GPIO_Port GPIOB
#define EXTEND_IO4_Pin GPIO_PIN_12
#define EXTEND_IO4_GPIO_Port GPIOB
#define EXTEND_IO5_Pin GPIO_PIN_13
#define EXTEND_IO5_GPIO_Port GPIOB
#define EXTEND_IO6_Pin GPIO_PIN_14
#define EXTEND_IO6_GPIO_Port GPIOB
#define PWR_SPEED_Pin GPIO_PIN_15
#define PWR_SPEED_GPIO_Port GPIOB
#define PWR_LPTIM2_OUT_Pin GPIO_PIN_8
#define PWR_LPTIM2_OUT_GPIO_Port GPIOA
#define PWR_IO1_Pin GPIO_PIN_9
#define PWR_IO1_GPIO_Port GPIOA
#define PWR_IO2_Pin GPIO_PIN_10
#define PWR_IO2_GPIO_Port GPIOA
#define PWR_IO3_Pin GPIO_PIN_11
#define PWR_IO3_GPIO_Port GPIOA
#define PWR_IO4_Pin GPIO_PIN_12
#define PWR_IO4_GPIO_Port GPIOA
#define SYS_SWDIO_Pin GPIO_PIN_13
#define SYS_SWDIO_GPIO_Port GPIOA
#define SYS_SWCLK_Pin GPIO_PIN_14
#define SYS_SWCLK_GPIO_Port GPIOA
#define SYS_SWO_Pin GPIO_PIN_3
#define SYS_SWO_GPIO_Port GPIOB
#define EXTEND_IO7_Pin GPIO_PIN_4
#define EXTEND_IO7_GPIO_Port GPIOB
#define EXTEND_IO8_Pin GPIO_PIN_5
#define EXTEND_IO8_GPIO_Port GPIOB
#define BOOT_UART1_TX_Pin GPIO_PIN_6
#define BOOT_UART1_TX_GPIO_Port GPIOB
#define BOOT_UART1_RX_Pin GPIO_PIN_7
#define BOOT_UART1_RX_GPIO_Port GPIOB
#define ISO_CAN1_RXD_Pin GPIO_PIN_8
#define ISO_CAN1_RXD_GPIO_Port GPIOB
#define ISO_CAN1_TXD_Pin GPIO_PIN_9
#define ISO_CAN1_TXD_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
