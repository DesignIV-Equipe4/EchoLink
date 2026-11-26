#include "Drivers/radio.h"
#include <string.h>


/*
 * Hardware Note (RxDutyCycle & Preamble):
 * When using Radio_SetRxDutyCycle(dev, rx_time_ms, sleep_time_ms) during STM32 sleep,
 * ensure the TX preamble covers at least one full RX sleep cycle:
 *   T_preamble + T_header <= 2 * T_rxPeriod + T_sleepPeriod
 *   T_preamble > T_sleepPeriod + T_TCXO (guarantees detection upon wake-up)
 */

/* Total wake-up time from Sleep mode including 5ms TCXO stabilization + 3ms margin */
#define RADIO_TOTAL_WAKEUP_TIME_MS (SX1268_TCXO_SETUP_TIME_MS + 3U)

// Power table for PA adaptation
static const int8_t radio_pwr_lut[] = {
		SX1268_TX_PWR_13DBM_SETPOINT, // idx 0: -6 dBm = 13-14dBm
		SX1268_TX_PWR_19DBM_SETPOINT, // idx 1: -4 dBm = 19dBm
		SX1268_TX_PWR_24DBM_SETPOINT, // idx 2: -2 dBm = 24dBm
		SX1268_TX_PWR_27DBM_SETPOINT, // idx 3:  2 dBm = 27dBm
		SX1268_TX_PWR_29DBM_SETPOINT, // idx 4:  4 dBm = 29dBm
		SX1268_TX_PWR_31DBM_SETPOINT, // idx 5:  7 dBm = 31.5dBm
		SX1268_TX_PWR_33DBM_SETPOINT  // idx 6: 14 dBm = 33dBm
};
#define RADIO_E22_PWR_LEVEL_COUNT (sizeof(radio_pwr_lut) / sizeof(radio_pwr_lut[0]))

// Compute approximation of SNR with sf configuration
static int8_t Radio_GetLoraSnrFloor(sx1268_lora_sf_t sf) {
	if(sf >= SX1268_LORA_SF_MAX) return (-10);
    switch(sf) {
        case SX1268_LORA_SF5:
        	return (-2);
        case SX1268_LORA_SF6:
        	return (-5);
        case SX1268_LORA_SF7:
        	return (-7);
        case SX1268_LORA_SF8:
        	return (-10);
        case SX1268_LORA_SF9:
        	return (-12);
        case SX1268_LORA_SF10:
        	return (-15);
        case SX1268_LORA_SF11:
        	return (-17);
        case SX1268_LORA_SF12:
        	return (-20);
        default:
        	return (-10);
    }
}

static radio_err_t Radio_MapError(sx1268_err_t err) {
	if(err >= SX1268_ERR_MAX) return RADIO_ERR_MAX;

    switch(err) {
        case SX1268_OK:
        	return RADIO_OK;
        case SX1268_ERR_NULL_PTR:
        	return RADIO_ERR_NULL_PTR;
        case SX1268_ERR_INVALID_PARAM:
        	return RADIO_ERR_INVALID_PARAM;
        case SX1268_ERR_BUSY_TIMEOUT:
        	return RADIO_ERR_BUSY_TIMEOUT;
        case SX1268_ERR_SPI_TX:
        case SX1268_ERR_SPI_RX:
        	return RADIO_ERR_SPI_BUS;
        case SX1268_ERR_SPI_COMM:
        	return RADIO_ERR_NOT_COMMUNICATING;
        case SX1268_ERR_CMD_TIMEOUT:
        case SX1268_ERR_CMD_PROC:
        case SX1268_ERR_CMD_EXEC:
        	return RADIO_ERR_CMD_REJECTED;
        case SX1268_ERR_REG_VERIFY:
        case SX1268_ERR_PACKET_TYPE_VERIFY:
        	return RADIO_ERR_REG_MISMATCH;
        case SX1268_ERR_CALIB_FAILED:
        case SX1268_ERR_IMG_CALIB_FAILED:
        	return RADIO_ERR_CALIBRATION;
        case SX1268_ERR_XOSC_START:
        	return RADIO_ERR_TCXO_FAULT;
        case SX1268_ERR_PLL_LOCK:
        case SX1268_ERR_PA_RAMP:
        	return RADIO_ERR_RF_FAULT;
        case SX1268_ERR_BUFFER_OVERFLOW:
        	return RADIO_ERR_BUFFER_OVERFLOW;
        default:
        	return RADIO_ERR_SPI_BUS;
    }
}

// Return nearest index in radio_pwr_lut
static int8_t Radio_FindPowerIndex(int8_t setpoint_dbm) {
    for(uint8_t i = 0U; i < RADIO_E22_PWR_LEVEL_COUNT; i++) {
        if(setpoint_dbm <= radio_pwr_lut[i]) {
            return (int8_t)i;
        }
    }

    return (int8_t)(RADIO_E22_PWR_LEVEL_COUNT - 1U);
}

static int8_t Radio_ClampPower(int8_t power_dbm) {
    if(power_dbm > SX1268_TX_PWR_33DBM_SETPOINT) {
        return SX1268_TX_PWR_33DBM_SETPOINT;
    }
    if(power_dbm < SX1268_PA_MIN_POWER_DBM) {
        return SX1268_PA_MIN_POWER_DBM;
    }

    return power_dbm;
}

// Compute Low Data Rate Optimization flags
static uint8_t Radio_ComputeLdro(sx1268_lora_sf_t sf, sx1268_lora_bw_t bw) {
	uint32_t bw_hz = 125000UL;

    switch(bw) {
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

    // Symbol duration Ts_us = (2^SF * 1e6) / BW_Hz
    uint32_t ts_us = (uint32_t)(((1ULL << (uint8_t)sf) * 1000000ULL) / bw_hz);

    return (ts_us >= 16380UL) ? 1U : 0U;
}

// Internal helper to configure RX interrupts, convert timeout to RTC steps, and start RX
static radio_err_t Radio_StartRxInternal(radio_t *dev, uint32_t timeout_ms, bool boosted) {
    // Enable RX Done, CRC Error, Header Error, and Timeout interrupts on DIO1
    const uint16_t rx_irq_mask = (uint16_t)(SX1268_IRQ_RX_DONE | SX1268_IRQ_CRC_ERROR | SX1268_IRQ_HEADER_ERROR | SX1268_IRQ_RX_TX_TIMEOUT);
    sx1268_err_t sx_err = SX1268_SetDio1IrqParams(&dev->sx1268, rx_irq_mask);
    if(sx_err != SX1268_OK) {
		return Radio_MapError(sx_err);
	}

    // Convert timeout from milliseconds to 64 kHz RTC step
    uint32_t rtc_steps;
    if(timeout_ms == 0U) {
		rtc_steps = dev->rx_continuous_cfg ? RADIO_RECEIVE_CONTINUOUS_TIMEOUT : 0x000000UL;
	} else {
		rtc_steps = RADIO_MS_TO_RTC_STEPS(timeout_ms);
		if(rtc_steps > 0xFFFFFEUL) {
			rtc_steps = 0xFFFFFEUL; // clamp
		}
	}

	dev->rx_boosted = boosted;
	dev->state      = RADIO_STATE_RX_RUNNING;

	return Radio_MapError(SX1268_SetRx(&dev->sx1268, rtc_steps, boosted));
}


radio_err_t Radio_Init(radio_t *dev, const radio_events_t *events) {
    if(dev == NULL) {
        return RADIO_ERR_NULL_PTR;
    }

    if(events != NULL) {
        dev->events = *events;
    } else {
        memset(&dev->events, 0, sizeof(radio_events_t));
    }

    dev->state                = RADIO_STATE_IDLE;
	dev->irq_fired            = false;
	dev->tx_timeout_ms        = 3000U;
	dev->tx_power_dbm         = SX1268_TX_PWR_24DBM_SETPOINT;
	dev->rx_continuous_cfg    = true;
	dev->rx_boosted           = true;
	dev->cad_pending_after_rx = false;

	sx1268_err_t sx_err = SX1268_Init(&dev->sx1268);
	if(sx_err != SX1268_OK) {
		return Radio_MapError(sx_err);
	}

    // Set private LoraWan and set default to 33 dBm SMA output setpoint (+14 dBm internal PA) and mask all IRQs
	if((sx_err = SX1268_SetPublicNetwork(&dev->sx1268, false)) != SX1268_OK) {
		return Radio_MapError(sx_err);
	}
	if((sx_err = SX1268_SetTxParams(&dev->sx1268, dev->tx_power_dbm, SX1268_RAMP_200_US)) != SX1268_OK) {
		return Radio_MapError(sx_err);
	}
	if((sx_err = SX1268_SetDio1IrqParams(&dev->sx1268, SX1268_IRQ_NONE)) != SX1268_OK) {
		return Radio_MapError(sx_err);
	}

    return RADIO_OK;
}

radio_state_t Radio_GetStatus(radio_t *dev) {
    return dev->state;
}

// Hot-start initialization and restores STM32 context on sys_wkup pin
radio_err_t Radio_WakeupInit(radio_t *dev, const radio_events_t *events, const radio_lora_config_t *cfg) {
    if(cfg == NULL) {
        return RADIO_ERR_NULL_PTR;
    }

    if(events != NULL) {
        dev->events = *events;
    } else {
        memset(&dev->events, 0, sizeof(radio_events_t));
    }

    // Restore software context
    dev->sx1268.op_mode          = SX1268_MODE_STDBY_RC;
	dev->sx1268.packet_type      = SX1268_PACKET_TYPE_LORA;
	dev->sx1268.image_calibrated = true;
	dev->sx1268.rx_continuous    = cfg->rx_continuous;
	dev->rx_continuous_cfg       = cfg->rx_continuous;
	dev->rx_boosted              = cfg->rx_boosted;
	dev->tx_power_dbm            = Radio_ClampPower(cfg->tx_power_dbm);
	dev->tx_timeout_ms           = cfg->tx_timeout_ms;
	dev->cad_pending_after_rx    = false;

    dev->sx1268.lora_mod.spreading_factor = cfg->sf;
    dev->sx1268.lora_mod.bandwidth        = cfg->bw;
    dev->sx1268.lora_mod.coding_rate      = cfg->cr;
    dev->sx1268.lora_mod.ldro_enable      = Radio_ComputeLdro(cfg->sf, cfg->bw);

    dev->sx1268.lora_pkt.preamble_len = cfg->preamble_len;
    dev->sx1268.lora_pkt.header_type  = cfg->fixed_len ? SX1268_LORA_HEADER_IMPLICIT : SX1268_LORA_HEADER_EXPLICIT;
    dev->sx1268.lora_pkt.payload_len  = cfg->fixed_len ? cfg->payload_len : RADIO_MAX_PAYLOAD_LENGTH;
    dev->sx1268.lora_pkt.crc_mode     = cfg->crc_on    ? SX1268_LORA_CRC_ON          : SX1268_LORA_CRC_OFF;
    dev->sx1268.lora_pkt.invert_iq    = cfg->invert_iq ? SX1268_LORA_IQ_INVERTED     : SX1268_LORA_IQ_NORMAL;

    dev->state     = RADIO_STATE_RX_RUNNING;
    dev->irq_fired = true;

    return RADIO_OK;
}

// Configure LoRa link parameters
//TODO: add errors
radio_err_t Radio_ConfigureLora(radio_t *dev, const radio_lora_config_t *cfg) {
    if((dev == NULL) && (cfg == NULL)) {
        return RADIO_ERR_NULL_PTR;
    }

    radio_err_t err = RADIO_OK;
    sx1268_err_t sx_err = SX1268_OK;
    // Configuration must be performed in Standby mode
    err = Radio_Standby(dev);
    if(err != RADIO_OK) {
    	return err;
    }

    // Set RF carrier frequency and run image calibration
    sx_err = SX1268_SetRfFrequency(&dev->sx1268, cfg->freq_hz);
    if(sx_err != SX1268_OK) {
		return Radio_MapError(sx_err);
	}

    // Configure PA output power and ramp time
    err = Radio_SetTxPower(dev, cfg->tx_power_dbm);
    if(err != RADIO_OK) {
		return err;
	}

    // Stop RX timeout timer only upon valid LoRa Header detection
    sx_err = SX1268_SetStopTimerOnPreamble(&dev->sx1268, false);
    if(sx_err != SX1268_OK) {
		return Radio_MapError(sx_err);
	}

    // Set number of symbols to validate LoRa reception lock
    sx_err = SX1268_SetLoraSymbTimeout(&dev->sx1268, cfg->symb_timeout);
    if(sx_err != SX1268_OK) {
		return Radio_MapError(sx_err);
	}

    // Configure LoRa modulation parameters
    sx1268_lora_mod_params_t mod_params = {
        .spreading_factor = cfg->sf,
        .bandwidth        = cfg->bw,
        .coding_rate      = cfg->cr,
        .ldro_enable      = Radio_ComputeLdro(cfg->sf, cfg->bw)
    };
    sx_err = SX1268_SetLoraModulationParams(&dev->sx1268, &mod_params);
    if(sx_err != SX1268_OK) {
		return Radio_MapError(sx_err);
	}

    // Configure LoRa packet parameters
    sx1268_lora_pkt_params_t pkt_params = {
        .preamble_len = cfg->preamble_len,
        .header_type  = cfg->fixed_len ? SX1268_LORA_HEADER_IMPLICIT : SX1268_LORA_HEADER_EXPLICIT,
        .payload_len  = cfg->fixed_len ? cfg->payload_len : RADIO_MAX_PAYLOAD_LENGTH,
        .crc_mode     = cfg->crc_on    ? SX1268_LORA_CRC_ON          : SX1268_LORA_CRC_OFF,
        .invert_iq    = cfg->invert_iq ? SX1268_LORA_IQ_INVERTED     : SX1268_LORA_IQ_NORMAL
    };
    sx_err = SX1268_SetLoraPacketParams(&dev->sx1268, &pkt_params);
    if(sx_err != SX1268_OK) {
		return Radio_MapError(sx_err);
	}

    sx_err = SX1268_SetPublicNetwork(&dev->sx1268, cfg->public_network);
    if(sx_err != SX1268_OK) {
		return Radio_MapError(sx_err);
	}

    dev->rx_continuous_cfg    = cfg->rx_continuous;
	dev->sx1268.rx_continuous = cfg->rx_continuous;
	dev->rx_boosted           = cfg->rx_boosted;
	dev->tx_timeout_ms        = cfg->tx_timeout_ms;

    return RADIO_OK;
}

// Change RF carrier frequency
radio_err_t Radio_SetChannel(radio_t *dev, uint32_t freq_hz) {
    return Radio_MapError(SX1268_SetRfFrequency(&dev->sx1268, freq_hz));
}

// Change TX output power setpoint
radio_err_t Radio_SetTxPower(radio_t *dev, int8_t power_dbm) {
    int8_t safe_pwr = Radio_ClampPower(power_dbm);
    dev->tx_power_dbm = safe_pwr;

    return Radio_MapError(SX1268_SetTxParams(&dev->sx1268, safe_pwr, SX1268_RAMP_200_US));
}

// Change RX input power to boosted mode
void Radio_SetRxBoosted(radio_t *dev, bool boosted) {
    dev->rx_boosted = boosted;
}

// Switch between Public and Private LoRa SyncWord
radio_err_t Radio_SetPublicNetwork(radio_t *dev, bool is_public) {
	return Radio_MapError(SX1268_SetPublicNetwork(&dev->sx1268, is_public));
}

// Update maximum payload length in LoRa packet
radio_err_t Radio_SetMaxPayloadLength(radio_t *dev, uint8_t max_len) {
    dev->sx1268.lora_pkt.payload_len = max_len;
    return Radio_MapError(SX1268_SetLoraPacketParams(&dev->sx1268, &dev->sx1268.lora_pkt));
}

// Compute adaptation step of tx power needed
int8_t Radio_ComputeAdaptiveTxPower(radio_t *dev, int8_t current_setpoint_dbm, int8_t rssi_dbm, int8_t snr_db) {
    int8_t idx = Radio_FindPowerIndex(current_setpoint_dbm);
    int8_t snr_floor = Radio_GetLoraSnrFloor(dev->sx1268.lora_mod.spreading_factor);
    int8_t snr_margin = (int8_t)(snr_db - snr_floor);
    int8_t step = 0;

    // Priority given to link reliability
    if((snr_margin < RADIO_TPC_SNR_MARGIN_CRIT_DB) || (rssi_dbm < RADIO_TPC_RSSI_CRITICAL_DBM)) {
        step = 2;
    } else if((snr_margin < RADIO_TPC_SNR_MARGIN_LOW_DB) || (rssi_dbm < RADIO_TPC_RSSI_LOW_DBM)) {
        step = 1;
    }
    // Protection against saturation at very short range
    else if(rssi_dbm > RADIO_TPC_RSSI_SATURATION_DBM) {
        step = -2;
    }
    // Power-saving only if RSSI and SNR margin are in hysteresis
    else if((snr_margin > RADIO_TPC_SNR_MARGIN_HIGH_DB) && (rssi_dbm > RADIO_TPC_RSSI_HIGH_DBM)) {
        step = -1;
    }

    idx = (int8_t)(idx + step);
    if(idx < 0) {
        idx = 0;
    } else if(idx >= (int8_t)RADIO_E22_PWR_LEVEL_COUNT) {
        idx = (int8_t)(RADIO_E22_PWR_LEVEL_COUNT - 1U);
    }

    return radio_pwr_lut[idx];
}

// Compute adaptation step of rx boost needed
bool Radio_ComputeAdaptiveRxBoost(radio_t *dev, bool current_boosted, int8_t rssi_dbm, int8_t snr_db) {
    int8_t snr_floor  = Radio_GetLoraSnrFloor(dev->sx1268.lora_mod.spreading_factor);
    int8_t snr_margin = (int8_t)(snr_db - snr_floor);

    // Boosted gain if weak signal
    if((rssi_dbm < RADIO_RX_BOOST_ON_RSSI_DBM) || (snr_margin < RADIO_RX_BOOST_ON_SNR_MARGIN_DB)) {
        return true;
    }

    // Power-saving only if RSSI and SNR margin are in hysteresis
    if((rssi_dbm > RADIO_RX_BOOST_OFF_RSSI_DBM) && (snr_margin > RADIO_RX_BOOST_OFF_SNR_MARGIN_DB)) {
        return false;
    }

    return current_boosted;
}

// Compute and apply new tx power
int8_t Radio_AdjustTxPower(radio_t *dev, int8_t rssi_dbm, int8_t snr_db) {
    int8_t target_pwr = Radio_ComputeAdaptiveTxPower(dev, dev->tx_power_dbm, rssi_dbm, snr_db);
    if(target_pwr != dev->tx_power_dbm) {
        Radio_SetTxPower(dev, target_pwr);
    }
    return dev->tx_power_dbm;
}

// Compute and apply new rx power
bool Radio_AdjustRxBoost(radio_t *dev, int8_t rssi_dbm, int8_t snr_db) {
    dev->rx_boosted = Radio_ComputeAdaptiveRxBoost(dev, dev->rx_boosted, rssi_dbm, snr_db);
    return dev->rx_boosted;
}

// Verify if target frequency is within hardware limits
bool Radio_CheckRfFrequency(uint32_t freq_hz) {
    return Radio_MapError(SX1268_CheckRfFrequency(freq_hz));
}

// Calculate packet Time-on-Air
uint32_t Radio_GetTimeOnAir(radio_t *dev, uint8_t pkt_len) {
    return Radio_MapError(SX1268_GetLoraTimeOnAir(&dev->sx1268, pkt_len));
}

// Return total wake-up time from Sleep mode
uint32_t Radio_GetWakeupTime(void) {
    return RADIO_TOTAL_WAKEUP_TIME_MS;
}

// Perform Listen-Before-Talk RSSI
// TODO: add errors
bool Radio_IsChannelFree(radio_t *dev, uint32_t freq_hz, int16_t rssi_thresh_dbm, uint32_t carrier_sense_time_ms) {
    // Enter continuous RX mode
	Radio_Standby(dev);
	if(!Radio_SetChannel(dev, freq_hz)) {
		return false;
	}

	// Temporary rx_continuous
	SX1268_SetDio1IrqParams(&dev->sx1268, SX1268_IRQ_NONE);
	SX1268_SetRx(&dev->sx1268, RADIO_RECEIVE_CONTINUOUS_TIMEOUT, dev->rx_boosted);
	HAL_Delay(1U);

    // Sample instantaneous RSSI
    bool is_free = true;
    for(uint32_t elapsed = 0U; elapsed < carrier_sense_time_ms; elapsed++) {
        int8_t rssi = Radio_GetRssiInstant(dev);
        if((int16_t)rssi > rssi_thresh_dbm) {
            is_free = false;
            break;
        }
        HAL_Delay(1U);
    }

    Radio_Standby(dev);
    dev->sx1268.rx_continuous = dev->rx_continuous_cfg;

    return is_free;
}

// Generate a 32-bit true random number
// TODO: add errors
uint32_t Radio_GetRandom(radio_t *dev) {
    // Mask all interrupts and put radio in continuous RX to feed RNG registers
    SX1268_SetDio1IrqParams(&dev->sx1268, SX1268_IRQ_NONE);
    SX1268_SetRx(&dev->sx1268, RADIO_RECEIVE_CONTINUOUS_TIMEOUT, false);

    HAL_Delay(1U);

    uint32_t rnd = SX1268_GetRandomHardware(&dev->sx1268);

    Radio_Standby(dev);
    dev->sx1268.rx_continuous = dev->rx_continuous_cfg;

    return rnd;
}

// Check if the physical DIO1
bool Radio_IsDio1High(radio_t *dev) {
    return (HAL_GPIO_ReadPin(dev->sx1268.dio1_port, dev->sx1268.dio1_pin) == GPIO_PIN_SET);
}

// Load payload into FIFO and start non-blocking packet transmission
//TODO: add errors
radio_err_t Radio_Send(radio_t *dev, const uint8_t *buffer, uint8_t size) {
    if(buffer == NULL) {
        return RADIO_ERR_NULL_PTR;
    }
    if(size == 0U) {
    	return RADIO_ERR_INVALID_PARAM;
    }

    sx1268_err_t sx_err = SX1268_OK;
    // Route TX Done and Timeout interrupts to DIO1
    const uint16_t tx_irq_mask = (uint16_t)(SX1268_IRQ_TX_DONE | SX1268_IRQ_RX_TX_TIMEOUT);
    sx_err = SX1268_SetDio1IrqParams(&dev->sx1268, tx_irq_mask);
    if(sx_err != SX1268_OK) {
    	return Radio_MapError(sx_err);
    }

    // Compute hardware watchdog timeout in RTC steps
    uint32_t timeout_steps = (dev->tx_timeout_ms > 0U) ? RADIO_MS_TO_RTC_STEPS(dev->tx_timeout_ms) : RADIO_TX_SINGLE_NO_TIMEOUT;
    sx_err = SX1268_SendPayload(&dev->sx1268, buffer, size, timeout_steps);
    if(sx_err != SX1268_OK) {
		return Radio_MapError(sx_err);
	}
    dev->state = RADIO_STATE_TX_RUNNING;

    return RADIO_OK;
}

// Start packet reception with standard power-saving LNA gain
radio_err_t Radio_Receive(radio_t *dev, uint32_t timeout_ms) {
    return Radio_StartRxInternal(dev, timeout_ms, false);
}

// Start packet reception with maximum Boosted LNA sensitivity
radio_err_t Radio_ReceiveBoosted(radio_t *dev, uint32_t timeout_ms) {
    return Radio_StartRxInternal(dev, timeout_ms, true);
}

radio_err_t Radio_ReceiveAdaptive(radio_t *dev, uint32_t timeout_ms) {
    return Radio_StartRxInternal(dev, timeout_ms, dev->rx_boosted);
}

// Start a LoRa Channel Activity Detection scan
radio_err_t Radio_StartCad(radio_t *dev, sx1268_cad_exit_mode_t exit_mode, uint32_t rx_timeout_ms) {
    // Enable both CAD and RX interrupts
	sx1268_err_t sx_err = SX1268_OK;
    const uint16_t cad_irq_mask = (uint16_t)(SX1268_IRQ_CAD_DONE | SX1268_IRQ_CAD_ACTIVITY_DETECTED | SX1268_IRQ_RX_DONE | SX1268_IRQ_CRC_ERROR | SX1268_IRQ_HEADER_ERROR | SX1268_IRQ_RX_TX_TIMEOUT);
    sx_err = SX1268_SetDio1IrqParams(&dev->sx1268, cad_irq_mask);
    if(sx_err != SX1268_OK) {
		return Radio_MapError(sx_err);
	}

    // Configure optimal CAD parameters and timeout
    uint32_t cad_timeout_steps = RADIO_MS_TO_RTC_STEPS(rx_timeout_ms);
    sx_err = SX1268_ConfigureOptimalCad(&dev->sx1268, exit_mode, cad_timeout_steps);
    if(sx_err != SX1268_OK) {
		return Radio_MapError(sx_err);
	}

    // Track if CAD automatically transitions to RX upon activity detection
    dev->cad_pending_after_rx = (exit_mode == SX1268_CAD_EXIT_RX);
    dev->state = RADIO_STATE_CAD;

    return Radio_MapError(SX1268_StartCad(&dev->sx1268));
}

// Start autonomous hardware RX / Sleep duty-cycled reception (sniff mode)
radio_err_t Radio_SetRxDutyCycle(radio_t *dev, uint32_t rx_time_ms, uint32_t sleep_time_ms) {
    const uint16_t rx_irq_mask = (uint16_t)(SX1268_IRQ_RX_DONE | SX1268_IRQ_CRC_ERROR | SX1268_IRQ_HEADER_ERROR | SX1268_IRQ_RX_TX_TIMEOUT);
    sx1268_err_t sx_err = SX1268_SetDio1IrqParams(&dev->sx1268, rx_irq_mask);
    if(sx_err != SX1268_OK) {
    	return sx_err;
    }

    uint32_t rx_steps    = RADIO_MS_TO_RTC_STEPS(rx_time_ms);
    uint32_t sleep_steps = RADIO_MS_TO_RTC_STEPS(sleep_time_ms);

    dev->state = RADIO_STATE_RX_RUNNING;
    dev->sx1268.rx_continuous = false;

    return Radio_MapError(SX1268_SetRxDutyCycle(&dev->sx1268, rx_steps, sleep_steps));
}

// Transmit an unmodulated continuous wave carrier (RF bench testing)
radio_err_t Radio_SetTxContinuousWave(radio_t *dev, uint32_t freq_hz, int8_t power_dbm) {
	sx1268_err_t sx_err = SX1268_SetRfFrequency(&dev->sx1268, freq_hz);
    if(sx_err != SX1268_OK) {
		return Radio_MapError(sx_err);
	}

    radio_err_t err = Radio_SetTxPower(dev, power_dbm);
    if(err != RADIO_OK) {
    	return err;
    }
    dev->state = RADIO_STATE_TX_RUNNING;

    return Radio_MapError(SX1268_SetTxContinuousWave(&dev->sx1268));
}

// Put radio into low-power Sleep mode with configuration retention (warm start)
radio_err_t Radio_Sleep(radio_t *dev) {
	sx1268_err_t sx_err = SX1268_SetSleep(&dev->sx1268, true);
    if(sx_err != SX1268_OK) {
		return Radio_MapError(sx_err);
	}
    dev->state = RADIO_STATE_IDLE;

    return RADIO_OK;
}

// Put radio into Standby RC mode
radio_err_t Radio_Standby(radio_t *dev) {
	sx1268_err_t sx_err = SX1268_SetStandby(&dev->sx1268, SX1268_STDBY_RC);
    if(sx_err != SX1268_OK) {
    	return Radio_MapError(sx_err);
    }
    dev->state = RADIO_STATE_IDLE;

    return RADIO_OK;
}

// Read instantaneous RSSI
int8_t Radio_GetRssiInstant(radio_t *dev) {
    return SX1268_GetRssiInstant(&dev->sx1268);
}


void Radio_OnDio1Irq(radio_t *dev) {
    dev->irq_fired = true;
}

void Radio_IrqProcess(radio_t *dev) {
	if((!dev->irq_fired) && (!Radio_IsDio1High(dev))) {
		return; // No available interrupt
	}

    dev->irq_fired = false;

    // Fetch and clear hardware IRQ status
    uint16_t irq_regs = SX1268_GetIrq(&dev->sx1268);
	if(irq_regs == SX1268_IRQ_NONE) {
		return;
	}

    // Handle TX Done
	if((irq_regs & SX1268_IRQ_TX_DONE) != 0U) {
		dev->state = RADIO_STATE_IDLE;
		if(dev->events.tx_done != NULL) {
			dev->events.tx_done();
		}
	}

    // Handle CAD Done
	if((irq_regs & SX1268_IRQ_CAD_DONE) != 0U) {
		bool detected = ((irq_regs & SX1268_IRQ_CAD_ACTIVITY_DETECTED) != 0U);

		if(detected && dev->cad_pending_after_rx) {
			dev->sx1268.op_mode = SX1268_MODE_RX;
			dev->state          = RADIO_STATE_RX_RUNNING;
		} else {
			SX1268_SetRxEnPin(dev->sx1268, false);
			dev->sx1268.op_mode = SX1268_MODE_STDBY_RC;
			dev->state          = RADIO_STATE_IDLE;
		}

		if(dev->events.cad_done != NULL) {
			dev->events.cad_done(detected);
		}
	}

    // Handle RX CRC/Header Errors vs Valid RX Done
	if((irq_regs & (SX1268_IRQ_CRC_ERROR | SX1268_IRQ_HEADER_ERROR)) != 0U) {
		if(!dev->sx1268.rx_continuous) {
			dev->state = RADIO_STATE_IDLE;
		}

		if(dev->events.rx_error != NULL) {
			dev->events.rx_error();
		}
	} else if((irq_regs & SX1268_IRQ_RX_DONE) != 0U) {
		uint8_t size = 0U;
		sx1268_lora_pkt_status_t pkt_status = { 0 };

		if(!dev->sx1268.rx_continuous) {
			dev->state = RADIO_STATE_IDLE;
		}

		if(SX1268_GetPayload(&dev->sx1268, dev->rx_buffer, &size, RADIO_MAX_PAYLOAD_LENGTH) == SX1268_OK) {
			if(SX1268_GetLoraPacketStatus(&dev->sx1268, &pkt_status) != SX1268_OK) {
				return;
			}

			if(dev->events.rx_done != NULL) {
				dev->events.rx_done(dev->rx_buffer, size, pkt_status.rssi_pkt_dbm, pkt_status.snr_pkt_db);
			}
		} else {
			return;
		}
	}

    // Handle Hardware RX or TX Timeout
	if((irq_regs & SX1268_IRQ_RX_TX_TIMEOUT) != 0U) {
		radio_state_t prev_state = dev->state;
		dev->state = RADIO_STATE_IDLE;

		if(prev_state == RADIO_STATE_TX_RUNNING) {
			if(dev->events.tx_timeout != NULL) {
				dev->events.tx_timeout();
			}
		} else if(prev_state == RADIO_STATE_RX_RUNNING) {
			if(dev->cad_pending_after_rx) {
				dev->cad_pending_after_rx = false;
				if(SX1268_SetSleep(&dev->sx1268, true) != SX1268_OK) {
					return;
				}
				if(SX1268_SetStandby(&dev->sx1268, SX1268_STDBY_RC) != SX1268_OK) {
					return;
				}
			}

			if(dev->events.rx_timeout != NULL) {
				dev->events.rx_timeout();
			}
		}
	}
}
