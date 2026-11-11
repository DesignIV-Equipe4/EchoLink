#ifndef DRIVERS_SX1268_H
#define DRIVERS_SX1268_H

#include <stdint.h>
#include <stdbool.h>


/* Constants SX1268 */

#define SX1268_XTAL_FREQ_HZ                 32000000UL
#define SX1268_FREQ_STEP_HZ                 0.95367431640625 /* 32e6 / 2^25 */

/* Frequency limits [410 ; 493] MHz */
#define SX1268_E22_FREQ_MIN_HZ              410000000UL
#define SX1268_E22_FREQ_MAX_HZ              493000000UL

/* Internal limits PA [-9 ; +22] dBm */
#define SX1268_PA_MIN_POWER_DBM             (-9)
#define SX1268_PA_MAX_POWER_DBM             (22)

/* Preamplification steps VCC 5V */
#define SX1268_E22_TX_PWR_13DBM_SETPOINT    (-6) /* -> ~13-14dBm SMA */
#define SX1268_E22_TX_PWR_19DBM_SETPOINT    (-4) /* -> ~19 dBm SMA */
#define SX1268_E22_TX_PWR_24DBM_SETPOINT    (-2) /* -> ~24 dBm SMA */
#define SX1268_E22_TX_PWR_27DBM_SETPOINT    (2)  /* -> ~27 dBm SMA */
#define SX1268_E22_TX_PWR_29DBM_SETPOINT    (4)  /* -> ~29 dBm SMA */
#define SX1268_E22_TX_PWR_31DBM_SETPOINT    (7)  /* -> ~31.5 dBm SMA */
#define SX1268_E22_TX_PWR_33DBM_SETPOINT    (14) /* -> 33 dBm SMA (max) */

/* Stabilization time 32MHz TCXO & NRST (ms) */
#define SX1268_NRST_HOLD_LOW_MS             2U
#define SX1268_NRST_RC_RISE_DELAY_MS        5U
#define SX1268_TCXO_SETUP_TIME_MS           5U
#define SX1268_MAX_FIFO_SIZE                256U

/* Register addresses */

#define SX1268_REG_RETENTION_LIST_0         0x029FU
#define SX1268_REG_RETENTION_LIST_1         0x02A0U
#define SX1268_REG_RETENTION_LIST_2         0x02A1U
#define SX1268_REG_WHITENING_MSB            0x06B8U
#define SX1268_REG_WHITENING_LSB            0x06B9U
#define SX1268_REG_CRC_SEED_MSB             0x06BCU
#define SX1268_REG_CRC_POLY_MSB             0x06BEU
#define SX1268_REG_SYNCWORD_FSK_BASE        0x06C0U
#define SX1268_REG_NODE_ADDR                0x06CDU
#define SX1268_REG_BROADCAST_ADDR           0x06CEU
#define SX1268_REG_LR_PAYLOAD_LENGTH        0x0702U
#define SX1268_REG_LR_PACKET_PARAMS         0x0704U
#define SX1268_REG_IQ_POLARITY              0x0736U
#define SX1268_REG_LR_SYNCWORD_MSB          0x0740U
#define SX1268_REG_LR_SYNCWORD_LSB          0x0741U
#define SX1268_REG_RNG_BASE                 0x0819U
#define SX1268_REG_TX_MODULATION            0x0889U
#define SX1268_REG_RX_GAIN                  0x08ACU
#define SX1268_REG_TX_CLAMP_CFG             0x08D8U
#define SX1268_REG_OCP                      0x08E7U
#define SX1268_REG_RTC_CTRL                 0x0902U
#define SX1268_REG_XTA_TRIM                 0x0911U
#define SX1268_REG_XTB_TRIM                 0x0912U
#define SX1268_REG_EVT_CLR                  0x0944U

/* Configuration values */
#define SX1268_RX_GAIN_POWER_SAVING         0x94U
#define SX1268_RX_GAIN_BOOSTED              0x96U
#define SX1268_OCP_140MA                    0x38U /* 140mA for PA > +14dBm */
#define SX1268_OCP_60MA                     0x18U /* 60mA for PA <= +14 dBm */

#define SX1268_LORA_SYNCWORD_PUBLIC         0x3444U
#define SX1268_LORA_SYNCWORD_PRIVATE        0x1424U

#define SX1268_CRC_IBM_SEED                 0xFFFFU
#define SX1268_CRC_CCITT_SEED               0x1D0FU
#define SX1268_CRC_POLY_IBM                 0x8005U
#define SX1268_CRC_POLY_CCITT               0x1021U

/* Fixed parameters SetPaConfig */
#define SX1268_PA_DEVICE_SEL_CONST          0x00U
#define SX1268_PA_LUT_CONST                 0x01U

/* Commands & Parameters */

typedef enum {
    SX1268_CMD_RESET_STATS                  = 0x00,
    SX1268_CMD_CLR_IRQ_STATUS               = 0x02,
    SX1268_CMD_CLR_DEVICE_ERRORS            = 0x07,
    SX1268_CMD_SET_DIO_IRQ_PARAMS           = 0x08,
    SX1268_CMD_WRITE_REGISTER               = 0x0D,
    SX1268_CMD_WRITE_BUFFER                 = 0x0E,
    SX1268_CMD_GET_STATS                    = 0x10,
    SX1268_CMD_GET_PACKET_TYPE              = 0x11,
    SX1268_CMD_GET_IRQ_STATUS               = 0x12,
    SX1268_CMD_GET_RX_BUFFER_STATUS         = 0x13,
    SX1268_CMD_GET_PACKET_STATUS            = 0x14,
    SX1268_CMD_GET_RSSI_INST                = 0x15,
    SX1268_CMD_GET_DEVICE_ERRORS            = 0x17,
    SX1268_CMD_READ_REGISTER                = 0x1D,
    SX1268_CMD_READ_BUFFER                  = 0x1E,
    SX1268_CMD_SET_STANDBY                  = 0x80,
    SX1268_CMD_SET_RX                       = 0x82,
    SX1268_CMD_SET_TX                       = 0x83,
    SX1268_CMD_SET_SLEEP                    = 0x84,
    SX1268_CMD_SET_RF_FREQUENCY             = 0x86,
    SX1268_CMD_SET_CAD_PARAMS               = 0x88,
    SX1268_CMD_CALIBRATE                    = 0x89,
    SX1268_CMD_SET_PACKET_TYPE              = 0x8A,
    SX1268_CMD_SET_MODULATION_PARAMS        = 0x8B,
    SX1268_CMD_SET_PACKET_PARAMS            = 0x8C,
    SX1268_CMD_SET_TX_PARAMS                = 0x8E,
    SX1268_CMD_SET_BUFFER_BASE_ADDR         = 0x8F,
    SX1268_CMD_SET_FALLBACK_MODE            = 0x93,
    SX1268_CMD_SET_RX_DUTY_CYCLE            = 0x94,
    SX1268_CMD_SET_PA_CONFIG                = 0x95,
    SX1268_CMD_SET_REGULATOR_MODE           = 0x96,
    SX1268_CMD_SET_TCXO_MODE                = 0x97,
    SX1268_CMD_CALIBRATE_IMAGE              = 0x98,
    SX1268_CMD_SET_RF_SWITCH_MODE           = 0x9D,
    SX1268_CMD_SET_STOP_TIMER_ON_PREAMBLE   = 0x9F,
    SX1268_CMD_SET_LORA_SYMB_TIMEOUT        = 0xA0,
    SX1268_CMD_GET_STATUS                   = 0xC0,
    SX1268_CMD_SET_FS                       = 0xC1,
    SX1268_CMD_SET_CAD                      = 0xC5,
    SX1268_CMD_SET_TX_CW                    = 0xD1,
    SX1268_CMD_SET_TX_INFINITE_PREAMBLE     = 0xD2
} sx1268_cmd_t;

typedef enum {
    SX1268_MODE_SLEEP                       = 0x00,
    SX1268_MODE_STDBY_RC,
    SX1268_MODE_STDBY_XOSC,
    SX1268_MODE_FS,
    SX1268_MODE_TX,
    SX1268_MODE_RX,
    SX1268_MODE_RX_DC,
    SX1268_MODE_CAD,
    SX1268_MODE_MAX
} sx1268_op_mode_t;

typedef enum {
    SX1268_STDBY_RC                         = 0x00,
    SX1268_STDBY_XOSC                       = 0x01,
    SX1268_STDBY_MAX
} sx1268_standby_mode_t;

typedef enum {
    SX1268_REGULATOR_LDO                    = 0x00,
    SX1268_REGULATOR_DCDC                   = 0x01,
    SX1268_REGULATOR_MAX
} sx1268_regulator_mode_t;

typedef enum {
    SX1268_PACKET_TYPE_GFSK                 = 0x00,
    SX1268_PACKET_TYPE_LORA                 = 0x01,
    SX1268_PACKET_TYPE_NONE                 = 0x0F,
} sx1268_packet_type_t;

typedef enum {
    SX1268_TCXO_1_6V                        = 0x00,
    SX1268_TCXO_1_7V                        = 0x01,
    SX1268_TCXO_1_8V                        = 0x02,
    SX1268_TCXO_2_2V                        = 0x03,
    SX1268_TCXO_2_4V                        = 0x04,
    SX1268_TCXO_2_7V                        = 0x05,
    SX1268_TCXO_3_0V                        = 0x06,
    SX1268_TCXO_3_3V                        = 0x07,
    SX1268_TCXO_MAX
} sx1268_tcxo_voltage_t;

typedef enum {
    SX1268_RAMP_10_US                       = 0x00,
    SX1268_RAMP_20_US                       = 0x01,
    SX1268_RAMP_40_US                       = 0x02,
    SX1268_RAMP_80_US                       = 0x03,
    SX1268_RAMP_200_US                      = 0x04,
    SX1268_RAMP_800_US                      = 0x05,
    SX1268_RAMP_1700_US                     = 0x06,
    SX1268_RAMP_3400_US                     = 0x07,
    SX1268_RAMP_MAX
} sx1268_ramp_time_t;

/* Speading factor -> balance between data rate and signal robustness 
 * SX1268_LORA_SF5 = high data rate, low sensitivity/range
 * SX1268_LORA_SF12 = low data rate, high sensitivity/range
 */
typedef enum {
    SX1268_LORA_MIN                         = 0x04,
    SX1268_LORA_SF5                         = 0x05,
    SX1268_LORA_SF6                         = 0x06,
    SX1268_LORA_SF7                         = 0x07,
    SX1268_LORA_SF8                         = 0x08,
    SX1268_LORA_SF9                         = 0x09,
    SX1268_LORA_SF10                        = 0x0A,
    SX1268_LORA_SF11                        = 0x0B,
    SX1268_LORA_SF12                        = 0x0C,
    SX1268_LORA_MAX
} sx1268_lora_sf_t;

typedef enum {
    SX1268_LORA_BW_007                      = 0x00, /* 7.81kHz */
    SX1268_LORA_BW_010                      = 0x08, /* 10.42kHz */
    SX1268_LORA_BW_015                      = 0x01, /* 15.63kHz */
    SX1268_LORA_BW_020                      = 0x09, /* 20.83kHz */
    SX1268_LORA_BW_031                      = 0x02, /* 31.25kHz */
    SX1268_LORA_BW_041                      = 0x0A, /* 41.67kHz */
    SX1268_LORA_BW_062                      = 0x03, /* 62.50kHz */
    SX1268_LORA_BW_125                      = 0x04, /* 125kHz */
    SX1268_LORA_BW_250                      = 0x05, /* 250kHz */
    SX1268_LORA_BW_500                      = 0x06  /* 500kHz */
} sx1268_lora_bw_t;

/* Coding rate -> error correction coding rate ratio
 * SX1268_LORA_CR_4_5 = 4 bits of information for 5 bits transmitted. 1 bit of redundancy
 * SX1268_LORA_CR_4_8 = 4 bits of information for 8 bits transmitted. 4 bit of redundancy
 */
typedef enum {
    SX1268_LORA_CR_4_5                      = 0x01,
    SX1268_LORA_CR_4_6                      = 0x02,
    SX1268_LORA_CR_4_7                      = 0x03,
    SX1268_LORA_CR_4_8                      = 0x04,
    SX1268_LORA_CR_MAX
} sx1268_lora_cr_t;

typedef enum {
    SX1268_LORA_HEADER_EXPLICIT             = 0x00, /* Variable header size */
    SX1268_LORA_HEADER_IMPLICIT             = 0x01, /* Fixed header size */
    SX1268_LORA_HEADER_MAX
} sx1268_lora_header_t;

typedef enum {
    SX1268_LORA_CRC_OFF                     = 0x00,
    SX1268_LORA_CRC_ON                      = 0x01,
    SX1268_LORA_CRC_MAX
} sx1268_lora_crc_t;

/* In-phase and Quadrature -> representation of amplitude and phase shifting */
typedef enum {
    SX1268_LORA_IQ_NORMAL                   = 0x00,
    SX1268_LORA_IQ_INVERTED                 = 0x01,
    SX1268_LORA_IQ_MAX
} sx1268_lora_iq_t;

/* Channel activity detection threshold -> number of symbol for activity detection on channel */
typedef enum {
    SX1268_CAD_01_SYMBOL                    = 0x00,
    SX1268_CAD_02_SYMBOL                    = 0x01,
    SX1268_CAD_04_SYMBOL                    = 0x02,
    SX1268_CAD_08_SYMBOL                    = 0x03,
    SX1268_CAD_16_SYMBOL                    = 0x04,
    SX1268_CAD_MAX
} sx1268_cad_symbols_t;

typedef enum {
    SX1268_CAD_EXIT_STDBY                   = 0x00,
    SX1268_CAD_EXIT_RX                      = 0x01,
    SX1268_CAD_EXIT_MAX
} sx1268_cad_exit_mode_t;

typedef enum {
    SX1268_IRQ_NONE                         = 0x0000U,
    SX1268_IRQ_TX_DONE                      = 0x0001U,
    SX1268_IRQ_RX_DONE                      = 0x0002U,
    SX1268_IRQ_PREAMBLE_DETECTED            = 0x0004U,
    SX1268_IRQ_SYNCWORD_VALID               = 0x0008U,
    SX1268_IRQ_HEADER_VALID                 = 0x0010U,
    SX1268_IRQ_HEADER_ERROR                 = 0x0020U,
    SX1268_IRQ_CRC_ERROR                    = 0x0040U,
    SX1268_IRQ_CAD_DONE                     = 0x0080U,
    SX1268_IRQ_CAD_ACTIVITY_DETECTED        = 0x0100U,
    SX1268_IRQ_RX_TX_TIMEOUT                = 0x0200U,
    SX1268_IRQ_ALL                          = 0xFFFFU
} sx1268_irq_mask_t;

/* State switch of external RF module */
typedef enum {
    SX1268_RFSW_OFF                         = 0, /* TXEN = 0, RXEN = 0 */
    SX1268_RFSW_RX                          = 1, /* TXEN = 0, RXEN = 1 */
    SX1268_RFSW_TX                          = 2, /* TXEN = 1, RXEN = 0 */
    SX1268_RFSW_MAX
} sx1268_rf_switch_state_t;

/* Driver confiruration & context structures */

typedef struct {
    sx1268_lora_sf_t        spreading_factor;
    sx1268_lora_bw_t        bandwidth;
    sx1268_lora_cr_t        coding_rate;
    uint8_t                 ldro_enable;
} sx1268_lora_mod_params_t;

typedef struct {
    uint16_t                preamble_len;
    sx1268_lora_header_t    header_type;
    uint8_t                 payload_len;
    sx1268_lora_crc_t       crc_mode;
    sx1268_lora_iq_t        invert_iq;
} sx1268_lora_pkt_params_t;

typedef struct {
    int8_t                  rssi_pkt_dbm;
    int8_t                  snr_pkt_db;
    int8_t                  signal_rssi_pkt_dbm;
} sx1268_lora_pkt_status_t;

// TODO: Hardware Abstraction Layer to be provided by stm32 (can be deleted when implmented)
typedef struct {
    void (*set_nrst_open_drain)(bool high_z);
    void (*set_rxen_pin)(bool enable);
    void (*wait_on_busy)(void);
    void (*delay_ms)(uint32_t ms);
    void (*wakeup_nss_pulse)(void);
    void (*spi_write_cmd)(sx1268_cmd_t cmd, const uint8_t *buf, uint16_t len);
    void (*spi_read_cmd)(sx1268_cmd_t cmd, uint8_t *buf, uint16_t len);
    void (*spi_write_reg)(uint16_t addr, const uint8_t *buf, uint16_t len);
    void (*spi_read_reg)(uint16_t addr, uint8_t *buf, uint16_t len);
    void (*spi_write_buf)(uint8_t offset, const uint8_t *buf, uint8_t len);
    void (*spi_read_buf)(uint8_t offset, uint8_t *buf, uint8_t len);
} sx1268_hal_t;

/* Main instance structure */
typedef struct {
    sx1268_hal_t                hal; // TODO: can be deleted when implmented
    sx1268_op_mode_t            op_mode;
    sx1268_packet_type_t        packet_type;
    sx1268_lora_mod_params_t    lora_mod;
    sx1268_lora_pkt_params_t    lora_pkt;
    bool                        image_calibrated;
    bool                        rx_continuous;
} sx1268_dev_t;

/* Prototypes */

bool SX1268_Init(sx1268_dev_t *dev);
void SX1268_Reset(sx1268_dev_t *dev);
void SX1268_CheckDeviceReady(sx1268_dev_t *dev);

void SX1268_SetSleep(sx1268_dev_t *dev, bool warm_start);
void SX1268_SetStandby(sx1268_dev_t *dev, sx1268_standby_mode_t mode);
void SX1268_SetFS(sx1268_dev_t *dev);
void SX1268_SetTx(sx1268_dev_t *dev, uint32_t timeout_rtc_steps);
void SX1268_SetRx(sx1268_dev_t *dev, uint32_t timeout_rtc_steps, bool rx_boosted);

bool SX1268_CheckRfFrequency(uint32_t freq_hz);
bool SX1268_SetRfFrequency(sx1268_dev_t *dev, uint32_t freq_hz);
void SX1268_CalibrateImage(sx1268_dev_t *dev, uint32_t freq_hz);

void SX1268_SetTxParams(sx1268_dev_t *dev, int8_t power_dbm, sx1268_ramp_time_t ramp_time);
void SX1268_SetLoraModulationParams(sx1268_dev_t *dev, const sx1268_lora_mod_params_t *params);
void SX1268_SetLoraPacketParams(sx1268_dev_t *dev, const sx1268_lora_pkt_params_t *params);
void SX1268_SetPublicNetwork(sx1268_dev_t *dev, bool is_public);

void SX1268_SetDio1IrqParams(sx1268_dev_t *dev, uint16_t irq_mask);
uint16_t SX1268_GetIrq(sx1268_dev_t *dev);

void SX1268_SendPayload(sx1268_dev_t *dev, const uint8_t *payload, uint8_t size, uint32_t timeout_rtc_steps);
bool SX1268_GetPayload(sx1268_dev_t *dev, uint8_t *buffer, uint8_t *size, uint8_t max_size);
void SX1268_GetLoraPacketStatus(sx1268_dev_t *dev, sx1268_lora_pkt_status_t *pkt_status);
int8_t SX1268_GetRssiInstant(sx1268_dev_t *dev);

void SX1268_ConfigureOptimalCad(sx1268_dev_t *dev, sx1268_cad_exit_mode_t exit_mode, uint32_t cad_timeout);
void SX1268_StartCad(sx1268_dev_t *dev);

uint32_t SX1268_GetLoraTimeOnAir(const sx1268_dev_t *dev, uint8_t pkt_len);


#endif /* DRIVERS_SX1268_H */
