#include "Drivers/sx1268.h"
#include <stddef.h>


static void SX1268_WriteRegByte(sx1268_dev_t *dev, uint16_t addr, uint8_t val) {
    SX1268_CheckDeviceReady(dev);
    dev->hal.spi_write_reg(addr, &val, 1U);
}

static uint8_t SX1268_ReadRegByte(sx1268_dev_t *dev, uint16_t addr) {
    uint8_t val = 0U;
    SX1268_CheckDeviceReady(dev);
    dev->hal.spi_read_reg(addr, &val, 1U);

    return val;
}

/* Modulation quality 500kHz bw */
static void SX1268_ApplyTxModulationWorkaround(sx1268_dev_t *dev) {
    uint8_t reg_val = SX1268_ReadRegByte(dev, SX1268_REG_TX_MODULATION);
    if((dev->packet_type == SX1268_PACKET_TYPE_LORA) && (dev->lora_mod.bandwidth == SX1268_LORA_BW_500)) {
        reg_val &= ~0x04U; /* Bit 2 = 0 */
    } else {
        reg_val |= 0x04U; /* Bit 2 = 1 */
    }

    SX1268_WriteRegByte(dev, SX1268_REG_TX_MODULATION, reg_val);
}

/* PA protection */
static void SX1268_ApplyPaClampWorkaround(sx1268_dev_t *dev) {
    uint8_t clamp_cfg = SX1268_ReadRegByte(dev, SX1268_REG_TX_CLAMP_CFG);
    clamp_cfg |= 0x1EU; // Set bits 4:1 to '1111' to optimize PA clamping threshold
    SX1268_WriteRegByte(dev, SX1268_REG_TX_CLAMP_CFG, clamp_cfg);
}

/* Stopping RTC counter after RX */
static void SX1268_ApplyRtcTimeoutWorkaround(sx1268_dev_t *dev) {
    // Write 0x00 to RTC Control to stop the RTC counter
    SX1268_WriteRegByte(dev, SX1268_REG_RTC_CTRL, 0x00U);

    uint8_t evt_clr = SX1268_ReadRegByte(dev, SX1268_REG_EVT_CLR);
    evt_clr |= 0x02U;   // Set bit 1 to clear any pending RTC timeout event
    SX1268_WriteRegByte(dev, SX1268_REG_EVT_CLR, evt_clr);
}

/* Inverted IQ polarity */
static void SX1268_ApplyIqPolarityWorkaround(sx1268_dev_t *dev, sx1268_lora_iq_t iq_mode) {
    uint8_t iq_reg = SX1268_ReadRegByte(dev, SX1268_REG_IQ_POLARITY);

    if(iq_mode == SX1268_LORA_IQ_INVERTED) {
        iq_reg &= (uint8_t)(~0x04U);
    } else {
        iq_reg |= 0x04U;
    }

    SX1268_WriteRegByte(dev, SX1268_REG_IQ_POLARITY, iq_reg);
}

/* Addition of the RxGain */
static void SX1268_EnableRxGainRetention(sx1268_dev_t *dev) {
    SX1268_WriteRegByte(dev, SX1268_REG_RETENTION_LIST_0, 0x01U);   // Enable 1 register slot in retention memory (0x029F)
    SX1268_WriteRegByte(dev, SX1268_REG_RETENTION_LIST_1, 0x08U);   // Store MSB of RxGain register address
    SX1268_WriteRegByte(dev, SX1268_REG_RETENTION_LIST_2, 0xACU);   // Store LSB of RxGain register address
}


void SX1268_Reset(sx1268_dev_t *dev) {
    // Turn off external LNA before resetting the chip (RXEN = 0)
    dev->hal.set_rxen_pin(false);

    // Reset with NRST
    dev->hal.set_nrst_open_drain(false);
    dev->hal.delay_ms(SX1268_NRST_HOLD_LOW_MS);
    dev->hal.set_nrst_open_drain(true);
    dev->hal.delay_ms(SX1268_NRST_RC_RISE_DELAY_MS);

    dev->hal.wait_on_busy();
    dev->op_mode = SX1268_MODE_STDBY_RC;
}

bool SX1268_Init(sx1268_dev_t *dev) {
    if(dev == NULL) return false;

    dev->image_calibrated = false; // Force image calibration on the first frequency setup
    dev->rx_continuous    = false; // Default to single RX mode

    // Hardware reset
    SX1268_Reset(dev);

    // Set Standby RC mode and enable DC-DC buck regulator adjustements
    SX1268_SetStandby(dev, SX1268_STDBY_RC);
    uint8_t reg_mode = (uint8_t)SX1268_REGULATOR_DCDC;
    SX1268_CheckDeviceReady(dev);
    dev->hal.spi_write_cmd(SX1268_CMD_SET_REGULATOR_MODE, &reg_mode, 1U);

    // DIO3 configuration for internal TXCO
    uint32_t tcxo_delay_steps = (uint32_t)SX1268_TCXO_SETUP_TIME_MS << 6U;
    uint8_t tcxo_buf[4] = {
        (uint8_t)SX1268_TCXO_2_2V,                      // Set DIO3 voltage to 2.2V
        (uint8_t)((tcxo_delay_steps >> 16U) & 0xFFU),   // 24-bit stabilization delay
        (uint8_t)((tcxo_delay_steps >> 8U)  & 0xFFU),
        (uint8_t)(tcxo_delay_steps & 0xFFU)
    };
    SX1268_CheckDeviceReady(dev);
    dev->hal.spi_write_cmd(SX1268_CMD_SET_TCXO_MODE, tcxo_buf, 4U); // Enable TCXO control on DIO3

    // Clear XOSC_START_ERR and calibrate
    uint8_t clr_err_buf[2] = { 0x00U, 0x00U };
    SX1268_CheckDeviceReady(dev);
    dev->hal.spi_write_cmd(SX1268_CMD_CLR_DEVICE_ERRORS, clr_err_buf, 2U); // Clear all device error flags

    uint8_t calib_all = 0x7FU;                                      // Bitmask 0x7F: calibrate RC64k, RC13M, PLL, ADC, and Image
    SX1268_CheckDeviceReady(dev);
    dev->hal.spi_write_cmd(SX1268_CMD_CALIBRATE, &calib_all, 1U);   // Start full calibration
    dev->hal.wait_on_busy();

    // DIO2 automatically drive external TXEN
    uint8_t dio2_rf_sw_enable = 0x01U;
    SX1268_CheckDeviceReady(dev);
    dev->hal.spi_write_cmd(SX1268_CMD_SET_RF_SWITCH_MODE, &dio2_rf_sw_enable, 1U);

    // PA over-voltage clamp fix & set Rx gain
    SX1268_ApplyPaClampWorkaround(dev);
    SX1268_EnableRxGainRetention(dev);

    // Set both TX/RX FIFO buffer base addresses to 0x00 (full 256-bytes range)
    uint8_t base_addr[2] = { 0x00U, 0x00U };
    SX1268_CheckDeviceReady(dev);
    dev->hal.spi_write_cmd(SX1268_CMD_SET_BUFFER_BASE_ADDR, base_addr, 2U);

    // Set Lora packet type
    dev->packet_type = SX1268_PACKET_TYPE_LORA;
    uint8_t pkt_type = (uint8_t)SX1268_PACKET_TYPE_LORA;
    SX1268_CheckDeviceReady(dev);
    dev->hal.spi_write_cmd(SX1268_CMD_SET_PACKET_TYPE, &pkt_type, 1U);

    return true;
}

// Ensure the radio is awake and ready to accept an SPI command
void SX1268_CheckDeviceReady(sx1268_dev_t *dev) {
    if((dev->op_mode == SX1268_MODE_SLEEP) || (dev->op_mode == SX1268_MODE_RX_DC)) {
        dev->hal.wakeup_nss_pulse();            // Pull NSS low then high to trigger wake-up
        dev->op_mode = SX1268_MODE_STDBY_RC;
    }
    dev->hal.wait_on_busy();
}

// Put the radio into low-power Sleep mode
void SX1268_SetSleep(sx1268_dev_t *dev, bool warm_start) {
    // Turn off external LNA (RXEN = 0, TXEN = 0 -> RF switch in CLOSE state)
    dev->hal.set_rxen_pin(false);

    uint8_t sleep_cfg = warm_start ? 0x04U : 0x00U; // 0x04 = keep configuration in retention RAM (warm start)
    SX1268_CheckDeviceReady(dev);
    dev->hal.spi_write_cmd(SX1268_CMD_SET_SLEEP, &sleep_cfg, 1U);
    dev->op_mode = SX1268_MODE_SLEEP;

    dev->hal.delay_ms(1U); // // Wait > 500 us
}

// Put the radio into Standby mode
void SX1268_SetStandby(sx1268_dev_t *dev, sx1268_standby_mode_t mode) {
    // Turn off external LNA (RXEN = 0, TXEN = 0 -> RF switch in CLOSE state)
    dev->hal.set_rxen_pin(false);

    uint8_t stdby_cfg = (uint8_t)mode;
    SX1268_CheckDeviceReady(dev);
    dev->hal.spi_write_cmd(SX1268_CMD_SET_STANDBY, &stdby_cfg, 1U);

    dev->op_mode = (mode == SX1268_STDBY_RC) ? SX1268_MODE_STDBY_RC : SX1268_MODE_STDBY_XOSC;
}

/* Put the radio into frequency synthesis mode (PLL locked, no TX/RX active)
 * Generate and switch communication channels with precised digital acquisition
 */
void SX1268_SetFS(sx1268_dev_t *dev) {
    // Turn off external LNA (RXEN = 0, TXEN = 0 -> RF switch in CLOSE state)
    dev->hal.set_rxen_pin(false);

    SX1268_CheckDeviceReady(dev);
    dev->hal.spi_write_cmd(SX1268_CMD_SET_FS, NULL, 0U);
    dev->op_mode = SX1268_MODE_FS;
}

// Start packet transmission
void SX1268_SetTx(sx1268_dev_t *dev, uint32_t timeout_rtc_steps) {
    // Force RXEN = 0 before DIO2 drives TXEN = 1
    dev->hal.set_rxen_pin(false);

    SX1268_ApplyTxModulationWorkaround(dev); // Apply 500kHz modulation fix before transmitting

    // Pack 24-bit timeout value (0x000000 = timeout disabled)
    uint8_t buf[3] = {
        (uint8_t)((timeout_rtc_steps >> 16U) & 0xFFU),
        (uint8_t)((timeout_rtc_steps >> 8U)  & 0xFFU),
        (uint8_t)(timeout_rtc_steps & 0xFFU)
    };

    SX1268_CheckDeviceReady(dev);
    dev->hal.spi_write_cmd(SX1268_CMD_SET_TX, buf, 3U); // DIO2 automatically sets TXEN = 1 and PA ramps up
    dev->op_mode = SX1268_MODE_TX;
}

// Start packet reception with selectable LNA gain
void SX1268_SetRx(sx1268_dev_t *dev, uint32_t timeout_rtc_steps, bool rx_boosted) {
    dev->rx_continuous = (timeout_rtc_steps == 0xFFFFFFUL); // 0xFFFFFF = continuous reception mode

    // 0x96 = max sensitivity (Boosted), 0x94 = lower current
    SX1268_WriteRegByte(dev, SX1268_REG_RX_GAIN, rx_boosted ? SX1268_RX_GAIN_BOOSTED : SX1268_RX_GAIN_POWER_SAVING);

    // Enable external LNA (RXEN = 1, TXEN = 0)
    dev->hal.set_rxen_pin(true);

    // Pack 24-bit timeout value (0x000000 = single no timeout, 0xFFFFFF = continuous)
    uint8_t buf[3] = {
        (uint8_t)((timeout_rtc_steps >> 16U) & 0xFFU),
        (uint8_t)((timeout_rtc_steps >> 8U)  & 0xFFU),
        (uint8_t)(timeout_rtc_steps & 0xFFU)
    };

    SX1268_CheckDeviceReady(dev);
    dev->hal.spi_write_cmd(SX1268_CMD_SET_RX, buf, 3U);
    dev->op_mode = SX1268_MODE_RX;
}

// Check if frequency is in supported band (410-493MHz)
bool SX1268_CheckRfFrequency(uint32_t freq_hz) {
    return ((freq_hz >= SX1268_E22_FREQ_MIN_HZ) && (freq_hz <= SX1268_E22_FREQ_MAX_HZ));
}

// Calibrate receiver image rejection for the target sub-GHz ISM band
void SX1268_CalibrateImage(sx1268_dev_t *dev, uint32_t freq_hz) {
    uint8_t cal_freq[2];
    if(freq_hz >= 460000000UL) {
        cal_freq[0] = 0x75U; // start 470-510MHz
        cal_freq[1] = 0x81U; // stop
    } else {
        cal_freq[0] = 0x6BU; // 430-440MHz
        cal_freq[1] = 0x6FU; // stop
    }

    SX1268_CheckDeviceReady(dev);
    dev->hal.spi_write_cmd(SX1268_CMD_CALIBRATE_IMAGE, cal_freq, 2U);
    dev->hal.wait_on_busy();
}

bool SX1268_SetRfFrequency(sx1268_dev_t *dev, uint32_t freq_hz) {
    if(!SX1268_CheckRfFrequency(freq_hz)) return false;

    // Run image calibration
    if(!dev->image_calibrated) {
        SX1268_CalibrateImage(dev, freq_hz);
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

    SX1268_CheckDeviceReady(dev);
    dev->hal.spi_write_cmd(SX1268_CMD_SET_RF_FREQUENCY, buf, 4U);

    return true;
}

// Configure internal PA and TX output power (-9;+22)dBm + ramp time
void SX1268_SetTxParams(sx1268_dev_t *dev, int8_t power_dbm, sx1268_ramp_time_t ramp_time) {
    uint8_t pa_cfg[4] = {
        0x04U,                          // paDutyCycle = 0x04 (never exceed 0x04)
        0x07U,                          // hpMax = 0x07 (max PA size capable of +22 dBm)
        SX1268_PA_DEVICE_SEL_CONST,     // deviceSel = 0x00
        SX1268_PA_LUT_CONST             // paLut = 0x01
    };

    SX1268_CheckDeviceReady(dev);
    dev->hal.spi_write_cmd(SX1268_CMD_SET_PA_CONFIG, pa_cfg, 4U);

    // Clamp power setpoint high-power PA
    if(power_dbm > SX1268_PA_MAX_POWER_DBM) {
        power_dbm = SX1268_PA_MAX_POWER_DBM;
    } else if(power_dbm < SX1268_PA_MIN_POWER_DBM) {
        power_dbm = SX1268_PA_MIN_POWER_DBM;
    }

    // Set Over-Current Protection to 140mA
    SX1268_WriteRegByte(dev, SX1268_REG_OCP, SX1268_OCP_140MA);

    // Send output power and PA ramp-up time
    uint8_t tx_buf[2] = { 
        (uint8_t)power_dbm, 
        (uint8_t)ramp_time 
    };
    SX1268_CheckDeviceReady(dev);
    dev->hal.spi_write_cmd(SX1268_CMD_SET_TX_PARAMS, tx_buf, 2U);
}

// Configure Lora modulation parameters (spreading factor, bandwidth, coding rate, low data rate optimization)
void SX1268_SetLoraModulationParams(sx1268_dev_t *dev, const sx1268_lora_mod_params_t *params) {
    dev->lora_mod = *params;

    // Protection Lora mode before setup
    if(dev->packet_type != SX1268_PACKET_TYPE_LORA) {
        dev->packet_type = SX1268_PACKET_TYPE_LORA;
        uint8_t pkt_type = (uint8_t)SX1268_PACKET_TYPE_LORA;
        SX1268_CheckDeviceReady(dev);
        dev->hal.spi_write_cmd(SX1268_CMD_SET_PACKET_TYPE, &pkt_type, 1U);
    }

    uint8_t buf[4] = {
        (uint8_t)params->spreading_factor,      // SF5 to SF12
        (uint8_t)params->bandwidth,             // 7.8 kHz to 500 kHz
        (uint8_t)params->coding_rate,           // CR 4/5 to 4/8
        params->ldro_enable ? 0x01U : 0x00U     // Low Data Rate Optimization (1 = ON when symbol time >= 16.38ms)
    };

    SX1268_CheckDeviceReady(dev);
    dev->hal.spi_write_cmd(SX1268_CMD_SET_MODULATION_PARAMS, buf, 4U);

    SX1268_ApplyTxModulationWorkaround(dev);
}

// Configure Lora frame format (preamble length, header mode, payload length, crc, iq polarity)
void SX1268_SetLoraPacketParams(sx1268_dev_t *dev, const sx1268_lora_pkt_params_t *params) {
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

    SX1268_CheckDeviceReady(dev);
    dev->hal.spi_write_cmd(SX1268_CMD_SET_PACKET_PARAMS, buf, 6U);

    SX1268_ApplyIqPolarityWorkaround(dev, params->invert_iq);
}

// Set Lora syncWord for public or private network
void SX1268_SetPublicNetwork(sx1268_dev_t *dev, bool is_public) {
    uint16_t syncword = is_public ? SX1268_LORA_SYNCWORD_PUBLIC : SX1268_LORA_SYNCWORD_PRIVATE;
    uint8_t buf[2] = {
        (uint8_t)((syncword >> 8U) & 0xFFU),    // SyncWord MSB at 0x0740
        (uint8_t)(syncword & 0xFFU)             // SyncWord LSB at 0x0741
    };

    SX1268_CheckDeviceReady(dev);
    dev->hal.spi_write_reg(SX1268_REG_LR_SYNCWORD_MSB, buf, 2U);
}

void SX1268_SetDio1IrqParams(sx1268_dev_t *dev, uint16_t irq_mask) {
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

    SX1268_CheckDeviceReady(dev);
    dev->hal.spi_write_cmd(SX1268_CMD_SET_DIO_IRQ_PARAMS, buf, 8U);
}

// Read active irq flags, clear them in hardware, and update RF switch/operating mode
uint16_t SX1268_GetIrq(sx1268_dev_t *dev) {
    uint8_t irq_buf[2] = { 0U, 0U };
    SX1268_CheckDeviceReady(dev);
    dev->hal.spi_read_cmd(SX1268_CMD_GET_IRQ_STATUS, irq_buf, 2U); // Read 16-bit irq status

    uint16_t irq_status = (uint16_t)(((uint16_t)irq_buf[0] << 8U) | irq_buf[1]);

    // Clear all triggered irq flags
    if(irq_status != 0U) {
        SX1268_CheckDeviceReady(dev);
        dev->hal.spi_write_cmd(SX1268_CMD_CLR_IRQ_STATUS, irq_buf, 2U);
    }

    if((irq_status & SX1268_IRQ_TX_DONE) != 0U) {
        dev->op_mode = SX1268_MODE_STDBY_RC;
    }

    // After single RX finishes or times out, turn off external LNA (RXEN = 0) and apply RTC fix
    if((irq_status & (SX1268_IRQ_RX_DONE | SX1268_IRQ_RX_TX_TIMEOUT | SX1268_IRQ_HEADER_ERROR)) != 0U) {
        if(!dev->rx_continuous) {
            dev->hal.set_rxen_pin(false);           // Turn off external LNA to save power in standby
            dev->op_mode = SX1268_MODE_STDBY_RC;
            SX1268_ApplyRtcTimeoutWorkaround(dev);  // Stop RTC timer
        }
    }

    return irq_status;
}

// Load payload into FIFO at offset 0x00 and start transmission
void SX1268_SendPayload(sx1268_dev_t *dev, const uint8_t *payload, uint8_t size, uint32_t timeout_rtc_steps) {
    dev->lora_pkt.payload_len = size;
    SX1268_SetLoraPacketParams(dev, &dev->lora_pkt);    // Send updated packet length to radio

    SX1268_CheckDeviceReady(dev);
    dev->hal.spi_write_buf(0x00U, payload, size);       // Write payload bytes into FIFO starting at offset 0x00

    SX1268_SetTx(dev, timeout_rtc_steps);
}

// Read received packet from FIFO buffer
bool SX1268_GetPayload(sx1268_dev_t *dev, uint8_t *buffer, uint8_t *size, uint8_t max_size) {
    uint8_t status[2] = { 0U, 0U };
    SX1268_CheckDeviceReady(dev);
    dev->hal.spi_read_cmd(SX1268_CMD_GET_RX_BUFFER_STATUS, status, 2U);

    uint8_t rx_len = status[0];     // Number of received bytes
    uint8_t rx_offset = status[1];  // FIFO offset where the received packet starts

    // Lora implicit header mode (bit 7 of 0x0704 == 1), read payload size from register 0x0702
    if((dev->packet_type == SX1268_PACKET_TYPE_LORA) && ((SX1268_ReadRegByte(dev, SX1268_REG_LR_PACKET_PARAMS) >> 7U) == 1U)) {
        rx_len = SX1268_ReadRegByte(dev, SX1268_REG_LR_PAYLOAD_LENGTH);
    }

    // Protection buffer overflow
    if(rx_len > max_size) return false;

    *size = rx_len;
    SX1268_CheckDeviceReady(dev);
    dev->hal.spi_read_buf(rx_offset, buffer, rx_len);

    return true;
}

// Get RSSI and SNR metrics of the last received Lora packet
void SX1268_GetLoraPacketStatus(sx1268_dev_t *dev, sx1268_lora_pkt_status_t *pkt_status) {
    uint8_t status[3] = { 0U, 0U, 0U };
    SX1268_CheckDeviceReady(dev);
    dev->hal.spi_read_cmd(SX1268_CMD_GET_PACKET_STATUS, status, 3U);

    pkt_status->rssi_pkt_dbm        = (int8_t)(-(int16_t)status[0] >> 1);   // Average packet RSSI = -RssiPkt / 2 (dBm)
    pkt_status->snr_pkt_db          = (int8_t)(((int8_t)status[1]) >> 2);   // Packet SNR = SnrPkt / 4 (dB, 2's complement)
    pkt_status->signal_rssi_pkt_dbm = (int8_t)(-(int16_t)status[2] >> 1);   // Despread Lora signal RSSI = -SignalRssiPkt / 2 (dBm)
}

// Get instantaneous RSSI
int8_t SX1268_GetRssiInstant(sx1268_dev_t *dev) {
    uint8_t raw_rssi = 0U;
    SX1268_CheckDeviceReady(dev);
    dev->hal.spi_read_cmd(SX1268_CMD_GET_RSSI_INST, &raw_rssi, 1U);
    return (int8_t)(-(int16_t)raw_rssi >> 1);
}

// Configure optimal Channel Activity Detection thresholds from Semtech AN1200.48
void SX1268_ConfigureOptimalCad(sx1268_dev_t *dev, sx1268_cad_exit_mode_t exit_mode, uint32_t cad_timeout) {
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

    SX1268_CheckDeviceReady(dev);
    dev->hal.spi_write_cmd(SX1268_CMD_SET_CAD_PARAMS, buf, 7U);
}

// Start a Lora channel activity detection scan
void SX1268_StartCad(sx1268_dev_t *dev) {
    dev->hal.set_rxen_pin(true);

    SX1268_CheckDeviceReady(dev);
    dev->hal.spi_write_cmd(SX1268_CMD_SET_CAD, NULL, 0U);
    dev->op_mode = SX1268_MODE_CAD;
}

// Compute LoRa packet Time-on-Air in milliseconds
uint32_t SX1268_GetLoraTimeOnAir(const sx1268_dev_t *dev, uint8_t pkt_len) {
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
