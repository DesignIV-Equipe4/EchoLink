/*
 * power_management.c
 *
 *  Created on: 26 nov. 2026
 *      Author: gagno
 */


#include "Systems/power_management.h"
#include "main.h"


#define POWERMGT_RAIL_POWER_SUPPLY_DELAY_MS 3000

// TODO: Need to be before HAL MX GPIO/SPI functions
static void PowerMgt_RailPowerSupply(void) {
	// Initial configuration
	GPIO_InitTypeDef GPIO_InitStruct = {0};
	GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
	GPIO_InitStruct.Pull = GPIO_NOPULL;

	// Lora pins on the same GPIO Port A
	GPIO_InitStruct.Pin = LORA_SPI1_NSS_Pin | LORA_SPI1_SCK_Pin | LORA_SPI1_MISO_Pin | LORA_SPI1_MOSI_Pin | LORA_INT_Pin | LORA_nRST_Pin;
	HAL_GPIO_Init(LORA_SPI1_SCK_GPIO_Port, &GPIO_InitStruct);

	// Lora pins on the same GPIO Port B
	GPIO_InitStruct.Pin = LORA_BUSY_Pin | LORA_RXEN_Pin;
	HAL_GPIO_Init(LORA_BUSY_GPIO_Port, &GPIO_InitStruct);

	HAL_Delay(POWERMGT_RAIL_POWER_SUPPLY_DELAY_MS);
}

void PowerMgt_Init(void) {
	PowerMgt_RailPowerSupply();

	return;
}

void PowerMgt_Update(void) {
	return;
}
