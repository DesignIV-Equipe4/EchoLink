#include "Drivers/sx1268.h"
#include <stddef.h>
#include <string.h>


static sx1268_err_t SX1268_DecodeStatus(uint8_t status) {
    if((status == 0x00U) || (status == 0xFFU)) {
        return SX1268_ERR_SPI_COMM;
    }

    uint8_t chip_mode = (uint8_t)((status & SX1268_STATUS_MODE_MASK) >> SX1268_STATUS_MODE_SHIFT);
    if((chip_mode < SX1268_CHIP_MODE_STDBY_RC) || (chip_mode > SX1268_CHIP_MODE_TX)) {
        return SX1268_ERR_SPI_COMM;
    }

    uint8_t cmd_status = (uint8_t)((status & SX1268_STATUS_CMD_MASK) >> SX1268_STATUS_CMD_SHIFT);
    switch(cmd_status) {
        case SX1268_CMD_STATUS_TIMEOUT:
        	return SX1268_ERR_CMD_TIMEOUT;
        case SX1268_CMD_STATUS_PROC_ERR:
        	return SX1268_ERR_CMD_PROC;
        case SX1268_CMD_STATUS_EXEC_FAIL:
        	return SX1268_ERR_CMD_EXEC;
        default:
        	return SX1268_OK;
    }
}

static sx1268_err_t SX1268_DecodeOpErrors(uint16_t op_errors) {
    if((op_errors & SX1268_OP_ERR_XOSC_START) != 0U) {
        return SX1268_ERR_XOSC_START;
    }
    if((op_errors & SX1268_OP_ERR_IMG_CALIB) != 0U) {
        return SX1268_ERR_IMG_CALIB_FAILED;
    }
    if((op_errors & (SX1268_OP_ERR_RC64K_CALIB | SX1268_OP_ERR_RC13M_CALIB | SX1268_OP_ERR_PLL_CALIB   | SX1268_OP_ERR_ADC_CALIB)) != 0U) {
        return SX1268_ERR_CALIB_FAILED;
    }
    if((op_errors & SX1268_OP_ERR_PLL_LOCK) != 0U) {
        return SX1268_ERR_PLL_LOCK;
    }
    if((op_errors & SX1268_OP_ERR_PA_RAMP) != 0U) {
        return SX1268_ERR_PA_RAMP;
    }

    return SX1268_OK;
}

static inline void SX1268_NssLow(sx1268_t *dev) {
	HAL_GPIO_WritePin(dev->nss_port, dev->nss_pin, GPIO_PIN_RESET);
}

static inline void SX1268_NssHigh(sx1268_t *dev) {
	HAL_GPIO_WritePin(dev->nss_port, dev->nss_pin, GPIO_PIN_SET);
}

static inline void SX1268_SetRxEnPin(sx1268_t *dev, bool enable) {
	HAL_GPIO_WritePin(dev->rxen_port, dev->rxen_pin, enable ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

static sx1268_err_t SX1268_WaitOnBusy(sx1268_t *dev) {
    uint32_t start_tick = HAL_GetTick();
    while(HAL_GPIO_ReadPin(dev->busy_port, dev->busy_pin) == GPIO_PIN_SET) {
        if((HAL_GetTick() - start_tick) >= SX1268_BUSY_TIMEOUT_MS) {
            return SX1268_ERR_BUSY_TIMEOUT; // Hardware fault or radio unresponsive
        }
    }

    return SX1268_OK;
}

static sx1268_err_t SX1268_SpiWriteCmd(sx1268_t *dev, sx1268_cmd_t cmd, const uint8_t *buf, uint16_t len) {
	sx1268_err_t err = SX1268_CheckDeviceReady(dev);
	if(err != SX1268_OK) {
		return err;
	}

    uint8_t opcode = (uint8_t)cmd;
    uint8_t rx_dummy = 0U;

    SX1268_NssLow(dev);
    if(HAL_SPI_TransmitReceive(dev->hspi, &opcode, &rx_dummy, 1U, SX1268_SPI_TIMEOUT_MS) != HAL_OK) {
		SX1268_NssHigh(dev);

		return SX1268_ERR_SPI_TX;
	}

    if((buf != NULL) && (len > 0U)) {
		uint8_t first_status = 0U;
		if(HAL_SPI_TransmitReceive(dev->hspi, (uint8_t *)&buf[0], &first_status, 1U, SX1268_SPI_TIMEOUT_MS) != HAL_OK) {
			SX1268_NssHigh(dev);
			return SX1268_ERR_SPI_TX;
		}
		if(len > 1U) {
			if(HAL_SPI_Transmit(dev->hspi, (uint8_t *)&buf[1], (uint16_t)(len - 1U), SX1268_SPI_TIMEOUT_MS) != HAL_OK) {
				SX1268_NssHigh(dev);
				return SX1268_ERR_SPI_TX;
			}
		}
		SX1268_NssHigh(dev);

		err = SX1268_DecodeStatus(first_status);
		if(err != SX1268_OK) {
			return err;
		}
	} else {
		SX1268_NssHigh(dev);
	}

	if(cmd != SX1268_CMD_SET_SLEEP) {
		return SX1268_WaitOnBusy(dev);
	}

	return SX1268_OK;
}

static sx1268_err_t SX1268_SpiReadCmd(sx1268_t *dev, sx1268_cmd_t cmd, uint8_t *buf, uint16_t len) {
    sx1268_err_t err = SX1268_CheckDeviceReady(dev);
    if(err != SX1268_OK) {
        return err;
    }

    uint8_t tx_hdr[2] = { (uint8_t)cmd, 0x00U };
    uint8_t rx_hdr[2] = { 0U, 0U };

    SX1268_NssLow(dev);
    if(HAL_SPI_TransmitReceive(dev->hspi, tx_hdr, rx_hdr, 2U, SX1268_SPI_TIMEOUT_MS) != HAL_OK) {
        SX1268_NssHigh(dev);
        return SX1268_ERR_SPI_TX;
    }

    if((buf != NULL) && (len > 0U)) {
        if(HAL_SPI_Receive(dev->hspi, buf, len, SX1268_SPI_TIMEOUT_MS) != HAL_OK) {
            SX1268_NssHigh(dev);
            return SX1268_ERR_SPI_RX;
        }
    }
    SX1268_NssHigh(dev);

    err = SX1268_DecodeStatus(rx_hdr[1]);
    if(err != SX1268_OK) {
        return err;
    }

    return SX1268_WaitOnBusy(dev);
}

static sx1268_err_t SX1268_SpiWriteReg(sx1268_t *dev, uint16_t addr, const uint8_t *buf, uint16_t len) {
    sx1268_err_t err = SX1268_CheckDeviceReady(dev);
    if(err != SX1268_OK) {
        return err;
    }

    uint8_t tx_hdr[3] = {
        (uint8_t)SX1268_CMD_WRITE_REGISTER,
        (uint8_t)((addr >> 8U) & 0xFFU),
        (uint8_t)(addr & 0xFFU)
    };
    uint8_t rx_hdr[3] = { 0U };

    SX1268_NssLow(dev);
    if(HAL_SPI_TransmitReceive(dev->hspi, tx_hdr, rx_hdr, 3U, SX1268_SPI_TIMEOUT_MS) != HAL_OK) {
        SX1268_NssHigh(dev);
        return SX1268_ERR_SPI_TX;
    }
    if((buf != NULL) && (len > 0U)) {
        if(HAL_SPI_Transmit(dev->hspi, (uint8_t *)buf, len, SX1268_SPI_TIMEOUT_MS) != HAL_OK) {
            SX1268_NssHigh(dev);
            return SX1268_ERR_SPI_TX;
        }
    }
    SX1268_NssHigh(dev);

    err = SX1268_DecodeStatus(rx_hdr[1]);
    if(err != SX1268_OK) {
        return err;
    }

    return SX1268_WaitOnBusy(dev);
}

static sx1268_err_t SX1268_SpiReadReg(sx1268_t *dev, uint16_t addr, uint8_t *buf, uint16_t len) {
    sx1268_err_t err = SX1268_CheckDeviceReady(dev);
    if(err != SX1268_OK) {
        return err;
    }

    uint8_t tx_hdr[4] = {
        (uint8_t)SX1268_CMD_READ_REGISTER,
        (uint8_t)((addr >> 8U) & 0xFFU),
        (uint8_t)(addr & 0xFFU),
        0x00U // NOP byte
    };
    uint8_t rx_hdr[4] = { 0U };

    SX1268_NssLow(dev);
    if(HAL_SPI_TransmitReceive(dev->hspi, tx_hdr, rx_hdr, 4U, SX1268_SPI_TIMEOUT_MS) != HAL_OK) {
        SX1268_NssHigh(dev);
        return SX1268_ERR_SPI_TX;
    }
    if((buf != NULL) && (len > 0U)) {
        if(HAL_SPI_Receive(dev->hspi, buf, len, SX1268_SPI_TIMEOUT_MS) != HAL_OK) {
            SX1268_NssHigh(dev);
            return SX1268_ERR_SPI_RX;
        }
    }
    SX1268_NssHigh(dev);

    err = SX1268_DecodeStatus(rx_hdr[1]);
    if(err != SX1268_OK) {
        return err;
    }

    return SX1268_WaitOnBusy(dev);
}

static sx1268_err_t SX1268_SpiWriteBuf(sx1268_t *dev, uint8_t offset, const uint8_t *buf, uint8_t len) {
    uint8_t header[2] = { (uint8_t)SX1268_CMD_WRITE_BUFFER, offset };

    sx1268_err_t err = SX1268_OK;
    err = SX1268_CheckDeviceReady(dev);
    if(err != SX1268_OK) {
		return err;
	}

    SX1268_NssLow(dev);
    if(HAL_SPI_Transmit(dev->hspi, header, 2U, SX1268_SPI_TIMEOUT_MS) != HAL_OK) {
    	SX1268_NssHigh(dev);
    	return SX1268_ERR_SPI_TX;
    }
    if((buf != NULL) && (len > 0U)) {
        if(HAL_SPI_Transmit(dev->hspi, (uint8_t *)buf, len, SX1268_SPI_TIMEOUT_MS) != HAL_OK) {
			SX1268_NssHigh(dev);
			return SX1268_ERR_SPI_TX;
        }
    }
    SX1268_NssHigh(dev);

    return SX1268_WaitOnBusy(dev);
}

static sx1268_err_t SX1268_SpiReadBuf(sx1268_t *dev, uint8_t offset, uint8_t *buf, uint8_t len) {
    uint8_t header[3] = {
        (uint8_t)SX1268_CMD_READ_BUFFER,
        offset,
        0x00U // NOP byte required before receiving FIFO data
    };

    sx1268_err_t err = SX1268_OK;
	err = SX1268_CheckDeviceReady(dev);
	if(err != SX1268_OK) {
		return err;
	}

    SX1268_NssLow(dev);
    if(HAL_SPI_Transmit(dev->hspi, header, 3U, SX1268_SPI_TIMEOUT_MS) != HAL_OK) {
    	SX1268_NssHigh(dev);
    	return SX1268_ERR_SPI_TX;
    }
    if((buf != NULL) && (len > 0U)) {
        if(HAL_SPI_Receive(dev->hspi, buf, len, SX1268_SPI_TIMEOUT_MS) != HAL_OK) {
        	SX1268_NssHigh(dev);
			return SX1268_ERR_SPI_RX;
        }
    }
    SX1268_NssHigh(dev);

    return SX1268_WaitOnBusy(dev);
}

static sx1268_err_t SX1268_WriteRegVerify(sx1268_t *dev, uint16_t addr, const uint8_t *buf, uint16_t len) {
    if((buf == NULL) || (len == 0U) || (len > 8U)) {
        return SX1268_ERR_INVALID_PARAM;
    }

    sx1268_err_t err = SX1268_SpiWriteReg(dev, addr, buf, len);
    if(err != SX1268_OK) {
        return err;
    }

    uint8_t read_buffer[8] = { 0U };
    err = SX1268_SpiReadReg(dev, addr, read_buffer, len);
    if(err != SX1268_OK) {
        return err;
    }

    if(memcmp(buf, read_buffer, len) != 0) {
        return SX1268_ERR_REG_VERIFY;
    }

    return SX1268_OK;
}

/* Modulation quality 500kHz bw */
static sx1268_err_t SX1268_ApplyTxModulationWorkaround(sx1268_t *dev) {
    uint8_t reg_val = 0U;
    sx1268_err_t err = SX1268_SpiReadReg(dev, SX1268_REG_TX_MODULATION, &reg_val, 1U);
    if(err != SX1268_OK) {
		return err;
	}

    if((dev->packet_type == SX1268_PACKET_TYPE_LORA) && (dev->lora_mod.bandwidth == SX1268_LORA_BW_500)) {
        reg_val &= ~0x04U; /* Bit 2 = 0 */
    } else {
        reg_val |= 0x04U; /* Bit 2 = 1 */
    }

    return SX1268_WriteRegVerify(dev, SX1268_REG_TX_MODULATION, &reg_val, 1U);
}

/* PA protection */
static sx1268_err_t SX1268_ApplyPaClampWorkaround(sx1268_t *dev) {
	uint8_t clamp_cfg = 0U;
	sx1268_err_t err = SX1268_SpiReadReg(dev, SX1268_REG_TX_CLAMP_CFG, &clamp_cfg, 1U);
	if(err != SX1268_OK) {
		return err;
	}
    clamp_cfg |= 0x1EU; // Set bits 4:1 to '1111' to optimize PA clamping threshold

    return SX1268_WriteRegVerify(dev, SX1268_REG_TX_CLAMP_CFG, &clamp_cfg, 1U);
}

// Stopping RTC counter after RX
/* TODO: verify execution, if fail:
 * replace 0x00 by &zero = 0x00
 */
static sx1268_err_t SX1268_ApplyRtcTimeoutWorkaround(sx1268_t *dev) {
	sx1268_err_t err = SX1268_OK;
    // Write 0x00 to RTC Control to stop the RTC counter
	err = SX1268_WriteRegVerify(dev, SX1268_REG_RTC_CTRL, 0x00U, 1U);
	if(err != SX1268_OK) {
		return err;
	}

    uint8_t evt_clr = 0U;
    err = SX1268_SpiReadReg(dev, SX1268_REG_EVT_CLR, &evt_clr, 1U);
    if(err != SX1268_OK) {
		return err;
	}
    evt_clr |= 0x02U;   // Set bit 1 to clear any pending RTC timeout event

    return SX1268_SpiWriteReg(dev, SX1268_REG_EVT_CLR, &evt_clr, 1U);
}

// Inverted IQ polarity
static sx1268_err_t SX1268_ApplyIqPolarityWorkaround(sx1268_t *dev, sx1268_lora_iq_t iq_mode) {
    uint8_t iq_reg = 0U;
    sx1268_err_t err = SX1268_SpiReadReg(dev, SX1268_REG_IQ_POLARITY, &iq_reg, 1U);
    if(err != SX1268_OK) {
		return err;
	}

    if(iq_mode == SX1268_LORA_IQ_INVERTED) {
        iq_reg &= (uint8_t)(~0x04U);
    } else {
        iq_reg |= 0x04U;
    }

    return SX1268_WriteRegVerify(dev, SX1268_REG_IQ_POLARITY, &iq_reg, 1U);
}

// Addition of the RxGain
static sx1268_err_t SX1268_EnableRxGainRetention(sx1268_t *dev) {
	const uint8_t buf[3] = {
			0x01U, 	// Enable 1 register slot in retention memory
			0x08U, 	// Store MSB of RxGain register address
			0xACU	// Store LSB of RxGain register address
	};
    return SX1268_WriteRegVerify(dev, SX1268_REG_RETENTION_LIST_0, buf, 3U);
}


sx1268_err_t SX1268_Reset(sx1268_t *dev) {
    // Turn off external LNA before resetting the chip (RXEN = 0)
	SX1268_SetRxEnPin(dev, false);
	SX1268_NssHigh(dev);

    // Reset with NRST
	HAL_GPIO_WritePin(dev->nrst_port, dev->nrst_pin, GPIO_PIN_RESET);
	HAL_Delay(SX1268_NRST_HOLD_LOW_MS);
	HAL_GPIO_WritePin(dev->nrst_port, dev->nrst_pin, GPIO_PIN_SET);
	HAL_Delay(SX1268_NRST_RC_RISE_DELAY_MS);

	sx1268_err_t err = SX1268_WaitOnBusy(dev);
	dev->op_mode = SX1268_MODE_STDBY_RC;

	return err;
}

sx1268_err_t SX1268_Init(sx1268_t *dev) {
	if((dev == NULL) || (dev->hspi == NULL) || (dev->rxen_port == NULL) || (dev->nss_port == NULL) || (dev->nrst_port == NULL) || (dev->busy_port == NULL) || (dev->dio1_port == NULL)) {
		return SX1268_ERR_NULL_PTR;
	}

    dev->image_calibrated = false; // Force image calibration on the first frequency setup
    dev->rx_continuous    = false; // Default to single RX mode

    sx1268_err_t err = SX1268_OK;
    // Hardware reset
    err = SX1268_Reset(dev);
	if(err != SX1268_OK) {
		return err;
	}

	err = SX1268_IsCommunicatingMcu(dev);
	if(err != SX1268_OK) {
		return err;
	}

	err = SX1268_WaitOnBusy(dev);
	if(err != SX1268_OK) {
		return err;
	}

    // Set Standby RC mode and enable DC-DC buck regulator adjustments
    err = SX1268_SetStandby(dev, SX1268_STDBY_RC);
    if(err != SX1268_OK) {
		return err;
	}

    uint8_t reg_mode = (uint8_t)SX1268_REGULATOR_DCDC;
    err = SX1268_SpiWriteCmd(dev, SX1268_CMD_SET_REGULATOR_MODE, &reg_mode, 1U);
    if(err != SX1268_OK) {
		return err;
	}

    // DIO3 configuration for internal TXCO
    uint32_t tcxo_delay_steps = (uint32_t)SX1268_TCXO_SETUP_TIME_MS << 6U;
    uint8_t tcxo_buf[4] = {
        (uint8_t)SX1268_TCXO_2_2V,                      // Set DIO3 voltage to 2.2V
        (uint8_t)((tcxo_delay_steps >> 16U) & 0xFFU),   // 24-bit stabilization delay
        (uint8_t)((tcxo_delay_steps >> 8U)  & 0xFFU),
        (uint8_t)(tcxo_delay_steps & 0xFFU)
    };
    err = SX1268_SpiWriteCmd(dev, SX1268_CMD_SET_TCXO_MODE, tcxo_buf, 4U); // Enable TCXO control on DIO3
    if(err != SX1268_OK) {
		return err;
	}

    // Clear XOSC_START_ERR and calibrate
    uint8_t clr_err_buf[2] = { 0x00U, 0x00U };
    err = SX1268_SpiWriteCmd(dev, SX1268_CMD_CLR_DEVICE_ERRORS, clr_err_buf, 2U);
    if(err != SX1268_OK) {
		return err;
	}

    uint8_t calib_all = 0x7FU;                                      		// Bitmask 0x7F: calibrate RC64k, RC13M, PLL, ADC, and Image
    err = SX1268_SpiWriteCmd(dev, SX1268_CMD_CALIBRATE, &calib_all, 1U); 	// Start full calibration
    if(err != SX1268_OK) {
		return err;
	}

    // DIO2 automatically drive external TXEN
    uint8_t dio2_rf_sw_enable = 0x01U;
    err = SX1268_SpiWriteCmd(dev, SX1268_CMD_SET_RF_SWITCH_MODE, &dio2_rf_sw_enable, 1U);
    if(err != SX1268_OK) {
		return err;
	}

    // PA over-voltage clamp fix & set Rx gain
    err = SX1268_ApplyPaClampWorkaround(dev);
    if(err != SX1268_OK) {
		return err;
	}
    err = SX1268_EnableRxGainRetention(dev);
    if(err != SX1268_OK) {
		return err;
	}

    // Set both TX/RX FIFO buffer base addresses to 0x00 (full 256-bytes range)
    uint8_t base_addr[2] = { 0x00U, 0x00U };
    err = SX1268_SpiWriteCmd(dev, SX1268_CMD_SET_BUFFER_BASE_ADDR, base_addr, 2U);
    if(err != SX1268_OK) {
		return err;
	}

    // Set Lora packet type
    dev->packet_type = SX1268_PACKET_TYPE_LORA;
    uint8_t pkt_type = (uint8_t)SX1268_PACKET_TYPE_LORA;
    err = SX1268_SpiWriteCmd(dev, SX1268_CMD_SET_PACKET_TYPE, &pkt_type, 1U);
    if(err != SX1268_OK) {
		return err;
	}

    return SX1268_OK;
}

// Ensure the radio is awake and ready to accept an SPI command
sx1268_err_t SX1268_CheckDeviceReady(sx1268_t *dev) {
    if((dev->op_mode == SX1268_MODE_SLEEP) || (dev->op_mode == SX1268_MODE_RX_DC)) {
    	SX1268_NssLow(dev);            // Pull NSS low then high to trigger wake-up
    	sx1268_err_t err = SX1268_WaitOnBusy(dev);
    	SX1268_NssHigh(dev);
    	if(err != SX1268_OK) {
			return err;
		}
        dev->op_mode = SX1268_MODE_STDBY_RC;
    }

    return SX1268_WaitOnBusy(dev);
}

sx1268_err_t SX1268_GetStatus(sx1268_t *dev) {
    sx1268_err_t err = SX1268_CheckDeviceReady(dev);
    if(err != SX1268_OK) {
        return err;
    }

    uint8_t tx[2] = { (uint8_t)SX1268_CMD_GET_STATUS, 0x00U };
    uint8_t rx[2] = { 0U, 0U };

    SX1268_NssLow(dev);
    if(HAL_SPI_TransmitReceive(dev->hspi, tx, rx, 2U, SX1268_SPI_TIMEOUT_MS) != HAL_OK) {
        SX1268_NssHigh(dev);
        return SX1268_ERR_SPI_TX;
    }
    SX1268_NssHigh(dev);

    return SX1268_DecodeStatus(rx[1]);
}

sx1268_err_t SX1268_GetDeviceErrors(sx1268_t *dev) {
    uint8_t buf[2] = { 0U, 0U };
    sx1268_err_t err = SX1268_SpiReadCmd(dev, SX1268_CMD_GET_DEVICE_ERRORS, buf, 2U);
    if(err != SX1268_OK) {
        return err;
    }

    uint16_t op_error = (uint16_t)(((uint16_t)buf[0] << 8U) | buf[1]);
    return SX1268_DecodeOpErrors(op_error);
}

sx1268_err_t SX1268_IsCommunicatingMcu(sx1268_t *dev) {
	// TODO: pair with conditional verifications if/else if (sleep state, run, ect..)
    return SX1268_GetStatus(dev);
}

// Put the radio into low-power Sleep mode
sx1268_err_t SX1268_SetSleep(sx1268_t *dev, bool warm_start) {
    // Turn off external LNA (RXEN = 0, TXEN = 0 -> RF switch in CLOSE state)
	SX1268_SetRxEnPin(dev, false);

    uint8_t sleep_cfg = warm_start ? 0x04U : 0x00U; // 0x04 = keep configuration in retention RAM (warm start)
    sx1268_err_t err = SX1268_SpiWriteCmd(dev, SX1268_CMD_SET_SLEEP, &sleep_cfg, 1U);
    if(err != SX1268_OK) {
		return err;
	}
    dev->op_mode = SX1268_MODE_SLEEP;

    HAL_Delay(1U); // // Wait > 500 us for context saving

    return SX1268_OK;
}

// Put the radio into Standby mode
sx1268_err_t SX1268_SetStandby(sx1268_t *dev, sx1268_standby_mode_t mode) {
    // Turn off external LNA (RXEN = 0, TXEN = 0 -> RF switch in CLOSE state)
	SX1268_SetRxEnPin(dev, false);

    uint8_t stdby_cfg = (uint8_t)mode;
    sx1268_err_t err = SX1268_SpiWriteCmd(dev, SX1268_CMD_SET_STANDBY, &stdby_cfg, 1U);
    if(err != SX1268_OK) {
		return err;
	}

    dev->op_mode = (mode == SX1268_STDBY_RC) ? SX1268_MODE_STDBY_RC : SX1268_MODE_STDBY_XOSC;

    return SX1268_OK;
}

/* Put the radio into frequency synthesis mode (PLL locked, no TX/RX active)
 * Generate and switch communication channels with precised digital acquisition
 */
sx1268_err_t SX1268_SetFS(sx1268_t *dev) {
    // Turn off external LNA (RXEN = 0, TXEN = 0 -> RF switch in CLOSE state)
	SX1268_SetRxEnPin(dev, false);
	sx1268_err_t err = SX1268_SpiWriteCmd(dev, SX1268_CMD_SET_FS, NULL, 0U);
	if(err != SX1268_OK) {
		return err;
	}
	dev->op_mode = SX1268_MODE_FS;

	return SX1268_OK;
}

// Start packet transmission
sx1268_err_t SX1268_SetTx(sx1268_t *dev, uint32_t timeout_rtc_steps) {
    // Force RXEN = 0 before DIO2 drives TXEN = 1
	SX1268_SetRxEnPin(dev, false);

	sx1268_err_t err = SX1268_OK;
	err = SX1268_ApplyTxModulationWorkaround(dev); // Apply 500kHz modulation fix before transmitting
    if(err != SX1268_OK) {
		return err;
	}

    // Pack 24-bit timeout value (0x000000 = timeout disabled)
    uint8_t buf[3] = {
        (uint8_t)((timeout_rtc_steps >> 16U) & 0xFFU),
        (uint8_t)((timeout_rtc_steps >> 8U)  & 0xFFU),
        (uint8_t)(timeout_rtc_steps & 0xFFU)
    };

    err = SX1268_SpiWriteCmd(dev, SX1268_CMD_SET_TX, buf, 3U); // DIO2 automatically sets TXEN = 1 and PA ramps up
    if(err != SX1268_OK) {
		return err;
	}
    dev->op_mode = SX1268_MODE_TX;

    return SX1268_OK;
}

// Start packet reception with selectable LNA gain
sx1268_err_t SX1268_SetRx(sx1268_t *dev, uint32_t timeout_rtc_steps, bool rx_boosted) {
    dev->rx_continuous = (timeout_rtc_steps == 0xFFFFFFUL); // 0xFFFFFF = continuous reception mode

    sx1268_err_t err = SX1268_OK;
    uint8_t sensitivity = rx_boosted ? SX1268_RX_GAIN_BOOSTED : SX1268_RX_GAIN_POWER_SAVING;
    // 0x96 = max sensitivity (Boosted), 0x94 = lower current
    err = SX1268_WriteRegVerify(dev, SX1268_REG_RX_GAIN, &sensitivity, 1U);
    if(err != SX1268_OK) {
		return err;
	}

    // Enable external LNA (RXEN = 1, TXEN = 0)
    SX1268_SetRxEnPin(dev, true);

    // Pack 24-bit timeout value (0x000000 = single no timeout, 0xFFFFFF = continuous)
    uint8_t buf[3] = {
        (uint8_t)((timeout_rtc_steps >> 16U) & 0xFFU),
        (uint8_t)((timeout_rtc_steps >> 8U)  & 0xFFU),
        (uint8_t)(timeout_rtc_steps & 0xFFU)
    };

    err = SX1268_SpiWriteCmd(dev, SX1268_CMD_SET_RX, buf, 3U);
    if(err != SX1268_OK) {
    	SX1268_SetRxEnPin(dev, false);
		return err;
	}
    dev->op_mode = SX1268_MODE_RX;

    return SX1268_OK;
}

// Start periodic Rx Duty Cycle (Listen / Sniff mode)
sx1268_err_t SX1268_SetRxDutyCycle(sx1268_t *dev, uint32_t rx_steps, uint32_t sleep_steps) {
    uint8_t buf[6] = {
        (uint8_t)((rx_steps >> 16U)    & 0xFFU),
        (uint8_t)((rx_steps >> 8U)     & 0xFFU),
        (uint8_t)(rx_steps             & 0xFFU),
        (uint8_t)((sleep_steps >> 16U) & 0xFFU),
        (uint8_t)((sleep_steps >> 8U)  & 0xFFU),
        (uint8_t)(sleep_steps          & 0xFFU)
    };

    SX1268_SetRxEnPin(dev, true);
    sx1268_err_t err = SX1268_SpiWriteCmd(dev, SX1268_CMD_SET_RX_DUTY_CYCLE, buf, 6U);
    if(err != SX1268_OK) {
    	SX1268_SetRxEnPin(dev, false);
		return err;
	}
    dev->op_mode = SX1268_MODE_RX_DC;

    return SX1268_OK;
}

// Start unmodulated RF Continuous Wave transmission (test mode)
sx1268_err_t SX1268_SetTxContinuousWave(sx1268_t *dev) {
    SX1268_SetRxEnPin(dev, false);
    sx1268_err_t err = SX1268_SpiWriteCmd(dev, SX1268_CMD_SET_TX_CW, NULL, 0U);
    if(err != SX1268_OK) {
		return err;
	}
    dev->op_mode = SX1268_MODE_TX;

    return SX1268_OK;
}

// Check if frequency is in supported band (410-493MHz)
bool SX1268_CheckRfFrequency(uint32_t freq_hz) {
    return ((freq_hz >= SX1268_E22_FREQ_MIN_HZ) && (freq_hz <= SX1268_E22_FREQ_MAX_HZ));
}

// Calibrate receiver image rejection for the target sub-GHz ISM band
sx1268_err_t SX1268_CalibrateImage(sx1268_t *dev, uint32_t freq_hz) {
    uint8_t cal_freq[2];
    if(freq_hz >= 460000000UL) {
        cal_freq[0] = 0x75U; // start 470-510MHz
        cal_freq[1] = 0x81U; // stop
    } else {
        cal_freq[0] = 0x6BU; // 430-440MHz
        cal_freq[1] = 0x6FU; // stop
    }

    return SX1268_SpiWriteCmd(dev, SX1268_CMD_CALIBRATE_IMAGE, cal_freq, 2U);
}

sx1268_err_t SX1268_SetRfFrequency(sx1268_t *dev, uint32_t freq_hz) {
    if(!SX1268_CheckRfFrequency(freq_hz)) return SX1268_ERR_INVALID_PARAM;

    // Run image calibration
    if(!dev->image_calibrated) {
    	sx1268_err_t err = SX1268_CalibrateImage(dev, freq_hz);
    	if(err != SX1268_OK) {
			return err;
		}
        dev->image_calibrated = true;
    }

    // Compute PLL step value using 64-bit integer and cast to 32bits
    uint32_t rf_freq_reg = (uint32_t)(((uint64_t)freq_hz << 25U) / SX1268_XTAL_FREQ_HZ);
    uint8_t buf[4] = {
        (uint8_t)((rf_freq_reg >> 24U) & 0xFFU),
        (uint8_t)((rf_freq_reg >> 16U) & 0xFFU),
        (uint8_t)((rf_freq_reg >> 8U)  & 0xFFU),
        (uint8_t)(rf_freq_reg & 0xFFU)
    };

    return SX1268_SpiWriteCmd(dev, SX1268_CMD_SET_RF_FREQUENCY, buf, 4U);
}

// Configure internal PA and TX output power (-9;+22)dBm + ramp time
sx1268_err_t SX1268_SetTxParams(sx1268_t *dev, int8_t power_dbm, sx1268_ramp_time_t ramp_time) {
    uint8_t pa_cfg[4] = {
        0x04U,                          // paDutyCycle = 0x04 (never exceed 0x04)
        0x07U,                          // hpMax = 0x07 (max PA size capable of +22 dBm)
        SX1268_PA_DEVICE_SEL_CONST,     // deviceSel = 0x00
        SX1268_PA_LUT_CONST             // paLut = 0x01
    };

    sx1268_err_t err = SX1268_OK;
    err = SX1268_SpiWriteCmd(dev, SX1268_CMD_SET_PA_CONFIG, pa_cfg, 4U);
    if(err != SX1268_OK) {
		return err;
	}

    // Clamp power setpoint high-power PA
    if(power_dbm > SX1268_PA_MAX_POWER_DBM) {
        power_dbm = SX1268_PA_MAX_POWER_DBM;
    } else if(power_dbm < SX1268_PA_MIN_POWER_DBM) {
        power_dbm = SX1268_PA_MIN_POWER_DBM;
    }

    // Set Over-Current Protection to 140mA
    uint8_t ocp = SX1268_OCP_140MA;
    err = SX1268_WriteRegVerify(dev, SX1268_REG_OCP, &ocp, 1U);
    if(err != SX1268_OK) {
		return err;
	}

    // Send output power and PA ramp-up time
    uint8_t tx_buf[2] = { 
        (uint8_t)power_dbm, 
        (uint8_t)ramp_time 
    };
    return SX1268_SpiWriteCmd(dev, SX1268_CMD_SET_TX_PARAMS, tx_buf, 2U);
}

// Configure Lora modulation parameters (spreading factor, bandwidth, coding rate, low data rate optimization)
sx1268_err_t SX1268_SetLoraModulationParams(sx1268_t *dev, const sx1268_lora_mod_params_t *params) {
    sx1268_err_t err = SX1268_OK;
    // Protection Lora mode before setup
    if(dev->packet_type != SX1268_PACKET_TYPE_LORA) {
        dev->packet_type = SX1268_PACKET_TYPE_LORA;
        uint8_t pkt_type = (uint8_t)SX1268_PACKET_TYPE_LORA;
        err = SX1268_SpiWriteCmd(dev, SX1268_CMD_SET_PACKET_TYPE, &pkt_type, 1U);
        if(err != SX1268_OK) {
			return err;
		}
    }

    dev->lora_mod = *params;

    uint8_t buf[4] = {
        (uint8_t)params->spreading_factor,      // SF5 to SF12
        (uint8_t)params->bandwidth,             // 7.8 kHz to 500 kHz
        (uint8_t)params->coding_rate,           // CR 4/5 to 4/8
        params->ldro_enable ? 0x01U : 0x00U     // Low Data Rate Optimization (1 = ON when symbol time >= 16.38ms)
    };

    err = SX1268_SpiWriteCmd(dev, SX1268_CMD_SET_MODULATION_PARAMS, buf, 4U);
    if(err != SX1268_OK) {
		return err;
	}

    return SX1268_ApplyTxModulationWorkaround(dev);
}

// Configure Lora frame format (preamble length, header mode, payload length, crc, iq polarity)
sx1268_err_t SX1268_SetLoraPacketParams(sx1268_t *dev, const sx1268_lora_pkt_params_t *params) {
    dev->lora_pkt = *params;

    // SF5 and SF6 require a minimum preamble of 12 symbols for reliable detection
    uint16_t preamble = params->preamble_len;
    if(((dev->lora_mod.spreading_factor == SX1268_LORA_SF5) || (dev->lora_mod.spreading_factor == SX1268_LORA_SF6)) && (preamble < 12U)) {
        preamble = 12U;
        dev->lora_pkt.preamble_len = 12U;
    }

    uint8_t buf[6] = {
        (uint8_t)((preamble >> 8U) & 0xFFU),    // Preamble length
        (uint8_t)(preamble & 0xFFU),
        (uint8_t)params->header_type,
        params->payload_len,
        (uint8_t)params->crc_mode,
        (uint8_t)params->invert_iq
    };

    sx1268_err_t err = SX1268_SpiWriteCmd(dev, SX1268_CMD_SET_PACKET_PARAMS, buf, 6U);
    if(err != SX1268_OK) {
		return err;
	}

    return SX1268_ApplyIqPolarityWorkaround(dev, params->invert_iq);
}

sx1268_err_t SX1268_SetStopTimerOnPreamble(sx1268_t *dev, bool enable) {
    uint8_t val = enable ? 0x01U : 0x00U;
    return SX1268_SpiWriteCmd(dev, SX1268_CMD_SET_STOP_TIMER_ON_PREAMBLE, &val, 1U);
}

// Configure number of LoRa symbols before RX timeout triggers if no lock occurs
sx1268_err_t SX1268_SetLoraSymbTimeout(sx1268_t *dev, uint8_t symb_num) {
    return SX1268_SpiWriteCmd(dev, SX1268_CMD_SET_LORA_SYMB_TIMEOUT, &symb_num, 1U);
}

// Set Lora syncWord for public or private network
sx1268_err_t SX1268_SetPublicNetwork(sx1268_t *dev, bool is_public) {
    uint16_t syncword = is_public ? SX1268_LORA_SYNCWORD_PUBLIC : SX1268_LORA_SYNCWORD_PRIVATE;
    const uint8_t buf[2] = {
    	(uint8_t)((syncword >> 8U) & 0xFFU),    // SyncWord MSB at 0x0740
		(uint8_t)(syncword & 0xFFU)             // SyncWord LSB at 0x0741
    };

    return SX1268_WriteRegVerify(dev, SX1268_REG_LR_SYNCWORD_MSB, buf, 2U);
}

sx1268_err_t SX1268_SetDio1IrqParams(sx1268_t *dev, uint16_t irq_mask) {
    /*
     * global irqmask : enables the requested interrupt sources
     * DIO1 Mask      : routes all enabled IRQs to the WKUP/EXTI pin
     * DIO2 Mask      : forced to 0x0000 because DIO2 is wired to TXEN (RF switch control)
     * DIO3 Mask      : forced to 0x0000 because DIO3 outputs 2.2V for the internal TCXO
     */
    uint8_t buf[8] = {
    	(uint8_t)((irq_mask >> 8U) & 0xFFU),    // Global IRQ mask
        (uint8_t)(irq_mask & 0xFFU),
        (uint8_t)((irq_mask >> 8U) & 0xFFU),    // DIO1 IRQ mask
        (uint8_t)(irq_mask & 0xFFU),
        0x00U,                                  // DIO2 IRQ mask
        0x00U,
        0x00U,                                  // DIO3 IRQ mask
        0x00U
    };

    return SX1268_SpiWriteCmd(dev, SX1268_CMD_SET_DIO_IRQ_PARAMS, buf, 8U);
}

// Read active irq flags, clear them in hardware, and update RF switch/operating mode
uint16_t SX1268_GetIrq(sx1268_t *dev) {
	sx1268_err_t err = SX1268_OK;
    uint8_t irq_buf[2] = { 0U, 0U };
    err = SX1268_SpiReadCmd(dev, SX1268_CMD_GET_IRQ_STATUS, irq_buf, 2U); // Read 16-bit irq status
    if(err != SX1268_OK) {
		return (uint16_t)(((uint16_t)irq_buf[0] << 8U) | irq_buf[1]);
	}

    uint16_t irq_status = (uint16_t)(((uint16_t)irq_buf[0] << 8U) | irq_buf[1]);
    // Clear all triggered irq flags
    if(irq_status != 0U) {
    	err = SX1268_SpiWriteCmd(dev, SX1268_CMD_CLR_IRQ_STATUS, irq_buf, 2U);
    	if(err != SX1268_OK) {
    		return (uint16_t)(((uint16_t)irq_buf[0] << 8U) | irq_buf[1]);
		}
    }
    if((irq_status & SX1268_IRQ_TX_DONE) != 0U) {
        dev->op_mode = SX1268_MODE_STDBY_RC;
    }
    // After single RX finishes or times out, turn off external LNA (RXEN = 0) and apply RTC fix
    if((irq_status & (SX1268_IRQ_RX_DONE | SX1268_IRQ_RX_TX_TIMEOUT | SX1268_IRQ_HEADER_ERROR)) != 0U) {
        if(dev->op_mode == SX1268_MODE_RX && !dev->rx_continuous) {
        	SX1268_SetRxEnPin(dev, false);           // Turn off external LNA to save power in standby
            dev->op_mode = SX1268_MODE_STDBY_RC;
            err = SX1268_ApplyRtcTimeoutWorkaround(dev);  // Stop RTC timer
            if(err != SX1268_OK) {
            	return (uint16_t)(((uint16_t)irq_buf[0] << 8U) | irq_buf[1]);
			}
        } else if(dev->op_mode == SX1268_MODE_TX) {
            dev->op_mode = SX1268_MODE_STDBY_RC;
        }
    }

    return irq_status;
}

// Load payload into FIFO at offset 0x00 and start transmission
sx1268_err_t SX1268_SendPayload(sx1268_t *dev, const uint8_t *payload, uint8_t size, uint32_t timeout_rtc_steps) {
	sx1268_err_t err = SX1268_OK;
    dev->lora_pkt.payload_len = size;
    err = SX1268_SetLoraPacketParams(dev, &dev->lora_pkt); // Send updated packet length to radio
    if(err != SX1268_OK) {
		return err;
	}

    err = SX1268_SpiWriteBuf(dev, 0x00U, payload, size); // Write payload bytes into FIFO starting at offset 0x00
    if(err != SX1268_OK) {
		return err;
	}

    return SX1268_SetTx(dev, timeout_rtc_steps);
}

// Read received packet from FIFO buffer
sx1268_err_t SX1268_GetPayload(sx1268_t *dev, uint8_t *buffer, uint8_t *size, uint8_t max_size) {
	sx1268_err_t err = SX1268_OK;
	uint8_t status[2] = { 0U, 0U };
	err = SX1268_SpiReadCmd(dev, SX1268_CMD_GET_RX_BUFFER_STATUS, status, 2U);
	if(err != SX1268_OK) {
		return err;
	}

    uint8_t rx_len = status[0];     // Number of received bytes
    uint8_t rx_offset = status[1];  // FIFO offset where the received packet starts

    // Protection buffer overflow
	if(rx_len > max_size) return SX1268_ERR_BUFFER_OVERFLOW;

    // Lora implicit header mode (bit 7 of 0x0704 == 1), read payload size from register 0x0702
	uint8_t rx_buf = 0U;
	err = SX1268_SpiReadReg(dev, SX1268_REG_LR_PACKET_PARAMS, &rx_buf, 1U);
	if(err != SX1268_OK) {
		return err;
	}
    if((dev->packet_type == SX1268_PACKET_TYPE_LORA) && (rx_buf >> 7U) == 1U) {
        err = SX1268_SpiReadReg(dev, SX1268_REG_LR_PAYLOAD_LENGTH, &rx_len, 1U);
        if(err != SX1268_OK) {
			return err;
		}
    }

    // Protection buffer overflow
	if(rx_len > max_size) return SX1268_ERR_BUFFER_OVERFLOW;

    *size = rx_len;
    return SX1268_SpiReadBuf(dev, rx_offset, buffer, rx_len);
}

// Get RSSI and SNR metrics of the last received Lora packet
sx1268_err_t SX1268_GetLoraPacketStatus(sx1268_t *dev, sx1268_lora_pkt_status_t *pkt_status) {
    uint8_t status[3] = { 0U, 0U, 0U };
    sx1268_err_t err = SX1268_SpiReadCmd(dev, SX1268_CMD_GET_PACKET_STATUS, status, 3U);
    if(err != SX1268_OK) {
		return err;
	}

    pkt_status->rssi_pkt_dbm        = (int8_t)(-(int16_t)status[0] >> 1);   // Average packet RSSI = -RssiPkt / 2 (dBm)
    pkt_status->snr_pkt_db          = (int8_t)(((int8_t)status[1]) >> 2);   // Packet SNR = SnrPkt / 4 (dB, 2's complement)
    pkt_status->signal_rssi_pkt_dbm = (int8_t)(-(int16_t)status[2] >> 1);   // Despread Lora signal RSSI = -SignalRssiPkt / 2 (dBm)

    return SX1268_OK;
}

// Get instantaneous RSSI
int8_t SX1268_GetRssiInstant(sx1268_t *dev) {
    uint8_t raw_rssi = 0U;
    if(SX1268_SpiReadCmd(dev, SX1268_CMD_GET_RSSI_INST, &raw_rssi, 1U) != SX1268_OK) {
    	return INT8_MIN;
    }

    return (int8_t)(-(int16_t)raw_rssi >> 1);
}

// Read 32-bit random number from hardware RNG registers
uint32_t SX1268_GetRandomHardware(sx1268_t *dev) {
    uint8_t buf[4] = { 0U, 0U, 0U, 0U };
    if(SX1268_SpiReadReg(dev, SX1268_REG_RNG_BASE, buf, 4U) != SX1268_OK) {
    	return UINT32_MAX;
    }

    return ((uint32_t)buf[0] << 24U) |
           ((uint32_t)buf[1] << 16U) |
           ((uint32_t)buf[2] << 8U)  |
           (uint32_t)buf[3];
}

// Configure optimal Channel Activity Detection thresholds from Semtech AN1200.48
sx1268_err_t SX1268_ConfigureOptimalCad(sx1268_t *dev, sx1268_cad_exit_mode_t exit_mode, uint32_t cad_timeout) {
    sx1268_cad_symbols_t cad_symbols = SX1268_CAD_04_SYMBOL;
    uint8_t det_peak = 24U;
    const uint8_t det_min = 10U; // Recommended by Semtech AN1200.48 (cadDetMin)

    // Based on AN1200.48 Table
    if(dev->lora_mod.bandwidth == SX1268_LORA_BW_500) {
        switch(dev->lora_mod.spreading_factor) {
            case SX1268_LORA_SF7:  
                cad_symbols = SX1268_CAD_04_SYMBOL;
                det_peak = 21U; 
                break;
            case SX1268_LORA_SF8:  
                cad_symbols = SX1268_CAD_04_SYMBOL; 
                det_peak = 22U; 
                break;
            case SX1268_LORA_SF9:  
                cad_symbols = SX1268_CAD_04_SYMBOL; 
                det_peak = 22U; 
                break;
            case SX1268_LORA_SF10: 
                cad_symbols = SX1268_CAD_04_SYMBOL; 
                det_peak = 23U; 
                break;
            case SX1268_LORA_SF11: 
                cad_symbols = SX1268_CAD_04_SYMBOL; 
                det_peak = 25U; 
                break;
            case SX1268_LORA_SF12:
            default:               
                cad_symbols = SX1268_CAD_08_SYMBOL;
                det_peak = 29U; 
                break;
        }
    } else {
        switch(dev->lora_mod.spreading_factor) {
            case SX1268_LORA_SF5:
            case SX1268_LORA_SF6:
            case SX1268_LORA_SF7:  
                cad_symbols = SX1268_CAD_02_SYMBOL; 
                det_peak = 22U; 
                break;
            case SX1268_LORA_SF8:  
                cad_symbols = SX1268_CAD_02_SYMBOL; 
                det_peak = 22U; 
                break;
            case SX1268_LORA_SF9:  
                cad_symbols = SX1268_CAD_04_SYMBOL; 
                det_peak = 23U; 
                break;
            case SX1268_LORA_SF10: 
                cad_symbols = SX1268_CAD_04_SYMBOL; 
                det_peak = 24U; 
                break;
            case SX1268_LORA_SF11: 
                cad_symbols = SX1268_CAD_04_SYMBOL; 
                det_peak = 25U; 
                break;
            case SX1268_LORA_SF12:
            default:               
                cad_symbols = SX1268_CAD_04_SYMBOL; 
                det_peak = 28U; 
                break;
        }
    }

    uint8_t buf[7] = {
        (uint8_t)cad_symbols,                       // Number of LoRa symbols to scan (1 to 16)
        det_peak,                                   // Correlation peak sensitivity threshold
        det_min,                                    // Minimum detection threshold
        (uint8_t)exit_mode,
        (uint8_t)((cad_timeout >> 16U) & 0xFFU),    // RX timeout
        (uint8_t)((cad_timeout >> 8U)  & 0xFFU),
        (uint8_t)(cad_timeout & 0xFFU)
    };

    return SX1268_SpiWriteCmd(dev, SX1268_CMD_SET_CAD_PARAMS, buf, 7U);
}

// Start a Lora channel activity detection scan
sx1268_err_t SX1268_StartCad(sx1268_t *dev) {
	SX1268_SetRxEnPin(dev, true);
	sx1268_err_t err = SX1268_SpiWriteCmd(dev, SX1268_CMD_SET_CAD, NULL, 0U);
	if(err != SX1268_OK) {
		SX1268_SetRxEnPin(dev, false);
		return err;
	}
	dev->op_mode = SX1268_MODE_CAD;

	return SX1268_OK;
}

// Compute LoRa packet Time-on-Air in milliseconds
uint32_t SX1268_GetLoraTimeOnAir(const sx1268_t *dev, uint8_t pkt_len) {
    uint32_t bw_hz = 125000UL;
    switch (dev->lora_mod.bandwidth) {
        case SX1268_LORA_BW_007: 
            bw_hz = 7810UL;   
            break;
        case SX1268_LORA_BW_010: 
            bw_hz = 10420UL;  
            break;
        case SX1268_LORA_BW_015: 
            bw_hz = 15630UL;  
            break;
        case SX1268_LORA_BW_020: 
            bw_hz = 20830UL;  
            break;
        case SX1268_LORA_BW_031: 
            bw_hz = 31250UL;  
            break;
        case SX1268_LORA_BW_041: 
            bw_hz = 41670UL;  
            break;
        case SX1268_LORA_BW_062: 
            bw_hz = 62500UL;  
            break;
        case SX1268_LORA_BW_125: 
            bw_hz = 125000UL; 
            break;
        case SX1268_LORA_BW_250: 
            bw_hz = 250000UL; 
            break;
        case SX1268_LORA_BW_500: 
            bw_hz = 500000UL; 
            break;
        default:                 
            bw_hz = 125000UL;
            break;
    }

    // Extract modulation and frame overhead parameters
    uint8_t sf = (uint8_t)dev->lora_mod.spreading_factor;
    uint8_t cr = (uint8_t)dev->lora_mod.coding_rate;
    int32_t n_bit_crc = (dev->lora_pkt.crc_mode == SX1268_LORA_CRC_ON) ? 16 : 0;
    int32_t n_sym_hdr = (dev->lora_pkt.header_type == SX1268_LORA_HEADER_EXPLICIT) ? 20 : 0;

    int32_t num;
    int32_t den;
    uint32_t preamble_overhead_x4;

    // Compute payload symbol numerator, denominator, and preamble overhead
    if((sf == 5U) || (sf == 6U)) {
        // preamble overhead = 6.25 symbols
        num = (8 * (int32_t)pkt_len) + n_bit_crc - (4 * (int32_t)sf) + n_sym_hdr;
        den = 4 * (int32_t)sf;
        preamble_overhead_x4 = 25U; // 6.25 * 4 = 25
    } else {
        // preamble overhead = 4.25 symbols
        num = (8 * (int32_t)pkt_len) + n_bit_crc - (4 * (int32_t)sf) + 8 + n_sym_hdr;
        den = 4 * ((int32_t)sf - (dev->lora_mod.ldro_enable ? 2 : 0));
        preamble_overhead_x4 = 17U; // 4.25 * 4 = 17
    }

    // Compute integer ceiling
    int32_t ceil_val = 0;
    if(num > 0) {
        ceil_val = ((num + den - 1) / den) * ((int32_t)cr + 4);
    }

    // Total frame symbols multiplied by 4: (N_preamble + overhead + 8 + payload_symbols) * 4
    uint32_t total_sym_x4 = ((uint32_t)dev->lora_pkt.preamble_len * 4U) + preamble_overhead_x4 + (8U * 4U) + ((uint32_t)ceil_val * 4U);

    // Convert total symbols to milliseconds: toa_ms = (total_sym_x4 / 4) * (2^SF * 1000 / bw_hz)
    uint64_t toa_num = (uint64_t)total_sym_x4 * (1ULL << sf) * 1000ULL;
    uint64_t toa_den = (uint64_t)bw_hz * 4ULL;

    // Rounded-up rime-on-air in milliseconds
    return (uint32_t)((toa_num + toa_den - 1ULL) / toa_den);
}
