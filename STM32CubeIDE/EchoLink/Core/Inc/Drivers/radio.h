#ifndef INC_DRIVERS_RADIO_H
#define INC_DRIVERS_RADIO_H

#include <Drivers/sx1268.h>
#include <stdint.h>
#include <stdbool.h>


/* Constants E22-400M33S*/
#define RADIO_MAX_PAYLOAD_LENGTH            (SX1268_MAX_FIFO_SIZE - 1U) // 255

#define RADIO_MS_TO_RTC_STEPS(ms)           ((uint32_t)(ms) << 6U)
#define RADIO_RECEIVE_CONTINUOUS_TIMEOUT    0xFFFFFFUL
#define RADIO_TX_SINGLE_NO_TIMEOUT          0x000000UL

// Tx threshold
#define RADIO_TPC_RSSI_CRITICAL_DBM     	(-112)
#define RADIO_TPC_RSSI_LOW_DBM          	(-100)
#define RADIO_TPC_RSSI_HIGH_DBM         	(-82)
#define RADIO_TPC_RSSI_SATURATION_DBM   	(-45)

#define RADIO_TPC_SNR_MARGIN_CRIT_DB    	(5)
#define RADIO_TPC_SNR_MARGIN_LOW_DB     	(10)
#define RADIO_TPC_SNR_MARGIN_HIGH_DB    	(18)

// Rx threshold
#define RADIO_RX_BOOST_ON_RSSI_DBM        	(-95)
#define RADIO_RX_BOOST_OFF_RSSI_DBM         (-78)

#define RADIO_RX_BOOST_ON_SNR_MARGIN_DB     (10)
#define RADIO_RX_BOOST_OFF_SNR_MARGIN_DB    (18)


/* Radio state machine */
typedef enum {
    RADIO_STATE_IDLE = 0,
    RADIO_STATE_RX_RUNNING,
    RADIO_STATE_TX_RUNNING,
    RADIO_STATE_CAD
} radio_state_t;

typedef enum {
    RADIO_OK = 0,
    RADIO_ERR_NULL_PTR,
    RADIO_ERR_INVALID_PARAM,
    RADIO_ERR_INVALID_FREQ,
    RADIO_ERR_NOT_COMMUNICATING,
    RADIO_ERR_BUSY_TIMEOUT,
    RADIO_ERR_SPI_BUS,
    RADIO_ERR_CMD_REJECTED,
    RADIO_ERR_REG_MISMATCH,
    RADIO_ERR_CALIBRATION,
    RADIO_ERR_TCXO_FAULT,
    RADIO_ERR_RF_FAULT,
    RADIO_ERR_BUFFER_OVERFLOW,
	RADIO_ERR_MAX
} radio_err_t;

/* Configuration */
typedef struct {
    uint32_t                freq_hz;
    int8_t                  tx_power_dbm;
    sx1268_lora_sf_t        sf;
    sx1268_lora_bw_t        bw;
    sx1268_lora_cr_t        cr;
    uint16_t                preamble_len;
    uint8_t                 symb_timeout;
    bool                    fixed_len;
    uint8_t                 payload_len;
    bool                    crc_on;
    bool                    invert_iq;
    bool                    public_network;
    bool                    rx_continuous;
    bool                    rx_boosted;
    uint32_t                tx_timeout_ms;
} radio_lora_config_t;

/* Event callbacks */
typedef struct {
	void (*tx_done)(void);
	void (*tx_timeout)(void);
	void (*rx_done)(const uint8_t *payload, uint8_t size, int8_t rssi_dbm, int8_t snr_db);
	void (*rx_timeout)(void);
	void (*rx_error)(void);
	void (*cad_done)(bool channel_activity_detected);
} radio_events_t;

/* Main instance structure */
typedef struct {
    sx1268_t                sx1268;
    radio_events_t          events;
    volatile radio_state_t  state;
    volatile bool           irq_fired;
    uint32_t                tx_timeout_ms;
    int8_t                  tx_power_dbm;
    bool                    rx_continuous_cfg;
    bool                    rx_boosted;
    bool                    cad_pending_after_rx;
    uint8_t                 rx_buffer[RADIO_MAX_PAYLOAD_LENGTH];
} radio_t;


/* Prototypes */
radio_err_t Radio_Init(radio_t *dev, const radio_events_t *events);
radio_state_t Radio_GetStatus(radio_t *dev);
radio_err_t Radio_WakeupInit(radio_t *dev, const radio_events_t *events, const radio_lora_config_t *cfg);

radio_err_t Radio_ConfigureLora(radio_t *dev, const radio_lora_config_t *cfg);
radio_err_t Radio_SetChannel(radio_t *dev, uint32_t freq_hz);
radio_err_t Radio_SetTxPower(radio_t *dev, int8_t power_dbm);
radio_err_t Radio_SetPublicNetwork(radio_t *dev, bool is_public);
radio_err_t Radio_SetMaxPayloadLength(radio_t *dev, uint8_t max_len);

int8_t Radio_ComputeAdaptiveTxPower(radio_t *dev, int8_t current_setpoint_dbm, int8_t rssi_dbm, int8_t snr_db);
bool Radio_ComputeAdaptiveRxBoost(radio_t *dev, bool current_boosted, int8_t rssi_dbm, int8_t snr_db);
int8_t Radio_AdjustTxPower(radio_t *dev, int8_t rssi_dbm, int8_t snr_db);
bool Radio_AdjustRxBoost(radio_t *dev, int8_t rssi_dbm, int8_t snr_db);

bool Radio_CheckRfFrequency(uint32_t freq_hz);
uint32_t Radio_GetTimeOnAir(radio_t *dev, uint8_t pkt_len);
uint32_t Radio_GetWakeupTime(void);

bool Radio_IsChannelFree(radio_t *dev, uint32_t freq_hz, int16_t rssi_thresh_dbm, uint32_t carrier_sense_time_ms);
uint32_t Radio_GetRandom(radio_t *dev);
bool Radio_IsDio1High(radio_t *dev);

radio_err_t Radio_Send(radio_t *dev, const uint8_t *buffer, uint8_t size);
radio_err_t Radio_Receive(radio_t *dev, uint32_t timeout_ms);
radio_err_t Radio_ReceiveBoosted(radio_t *dev, uint32_t timeout_ms);
radio_err_t Radio_ReceiveAdaptive(radio_t *dev, uint32_t timeout_ms);
radio_err_t Radio_StartCad(radio_t *dev, sx1268_cad_exit_mode_t exit_mode, uint32_t rx_timeout_ms);
radio_err_t Radio_SetRxDutyCycle(radio_t *dev, uint32_t rx_time_ms, uint32_t sleep_time_ms);
radio_err_t Radio_SetTxContinuousWave(radio_t *dev, uint32_t freq_hz, int8_t power_dbm);

radio_err_t Radio_Sleep(radio_t *dev);
radio_err_t Radio_Standby(radio_t *dev);
int8_t Radio_GetRssiInstant(radio_t *dev);

/* Interrupt handling */
void Radio_OnDio1Irq(radio_t *dev);
void Radio_IrqProcess(radio_t *dev);


#endif /* INC_DRIVERS_RADIO_H */
