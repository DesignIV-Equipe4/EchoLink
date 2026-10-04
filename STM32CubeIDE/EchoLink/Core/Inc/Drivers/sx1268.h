/*
 * sx1268.h
 *
 *  Created on: 6 sept. 2026
 *      Author: gagno
 *      Source: https://www.cdebyte.com/products/E22-400M33S
 */

#ifndef INC_DRIVERS_SX1268_H_
#define INC_DRIVERS_SX1268_H_


#include "stdio.h"
#include "math.h"


#define RADIO_WAKEUP_TIME_MS 				3 		// Radio complete Wake-up Time with margin for temperature compensation
#define RADIO_RESET_TIME_MS					50 		// Radio complete Reset Time with margin for signal stabilization in 20-30ms
#define AUTO_RX_TX_OFFSET_MS				2 		// Compensation delay for SetAutoTx/Rx functions in 15.625 microseconds
#define CRC_IBM_SEED 						0xFFFF 	// LFSR initial value to compute IBM type CRC
#define CRC_CCITT_SEED 						0x1D0F 	// LFSR initial value to compute CCIT type CRC
#define CRC_POLYNOMIAL_IBM 					0x8005 	// Polynomial used to compute IBM CRC
#define CRC_POLYNOMIAL_CCITT				0x1021 	// Polynomial used to compute CCIT CRC
#define REG_LR_CRCSEEDBASEADDR				0x06BC 	// The address of the register holding the first byte defining the CRC seed
#define REG_LR_CRCPOLYBASEADDR				0x06BE 	// The address of the register holding the first byte defining the CRC polynomial
#define REG_LR_WHITSEEDBASEADDR_MSB			0x06B8 	// The address of the register holding the first byte defining the whitening seed
#define REG_LR_WHITSEEDBASEADDR_LSB			0x06B9 	// The address of the register holding the first byte defining the whitening seed
#define REG_LR_PACKETPARAMS 				0x0704 	// The address of the register holding the packet configuration
#define REG_LR_PAYLOADLENGTH				0x0702 	// The address of the register holding the payload size
#define REG_LR_SYNCWORDBASEADDRESS			0x06C0 	// The addresses of the registers holding SyncWords values
#define REG_LR_SYNCWORD	 					0x0740 	// The addresses of the register holding LoRa Modem SyncWord value
#define LORA_MAC_PRIVATE_SYNCWORD			0x1424 	// Syncword for Private LoRa networks
#define LORA_MAC_PUBLIC_SYNCWORD 			0x3444 	// Syncword for Public LoRa networks
#define RANDOM_NUMBER_GENERATORBASEADDR		0x0819 	// The address of the register giving a 4 bytes random number
#define REG_RX_GAIN							0x08AC 	// The address of the register holding RX Gain value (0x94: power saving, 0x96: rx boosted)
#define REG_XTA_TRIM 						0x0911 	// Change the value on the device internal trimming capacitor
#define REG_OCP								0x08E7 	// Set the current max value in the over current protection

// Structure describing the radio status
typedef union radioStatus_u {
    uint8_t value;
    struct {
    	// bit order is lsb -> msb
        uint8_t reserved  : 1;  // Reserved
        uint8_t cmdStatus : 3;  // Command status
        uint8_t chipMode  : 3;  // Chip mode
        uint8_t cpuBusy   : 1;  // Flag for CPU radio busy
    } fields;
} radioStatus_t;

// Structure describing the error codes for callback functions
typedef enum {
    IRQ_HEADER_ERROR_CODE                   = 0x01,
    IRQ_SYNCWORD_ERROR_CODE                 = 0x02,
    IRQ_CRC_ERROR_CODE                      = 0x04,
} irqErrorCode_t;

enum irqPblSyncHeaderCode_t {
    IRQ_PBL_DETECT_CODE                     = 0x01,
    IRQ_SYNCWORD_VALID_CODE                 = 0x02,
    IRQ_HEADER_VALID_CODE                   = 0x04,
};

// Represents the operating mode the radio is actually running
typedef enum {
    MODE_SLEEP                              = 0x00,         // The radio is in sleep mode
    MODE_STDBY_RC,                                          // The radio is in standby mode with RC oscillator
    MODE_STDBY_XOSC,                                        // The radio is in standby mode with XOSC oscillator
    MODE_FS,                                                // The radio is in frequency synthesis mode
    MODE_TX,                                                // The radio is in transmit mode
    MODE_RX,                                                // The radio is in receive mode
    MODE_RX_DC,                                             // The radio is in receive duty cycle mode
    MODE_CAD                                                // The radio is in channel activity detection mode
} radioOperatingModes_t;

/*
 * Declares the oscillator in use while in standby mode
 *
 * Using the STDBY_RC standby mode allow to reduce the energy consumption
 * STDBY_XOSC should be used for time critical applications
 */
typedef enum {
    STDBY_RC                                = 0x00,
    STDBY_XOSC                              = 0x01,
} radioStandbyModes_t;

/*
 * Declares the power regulation used to power the device
 *
 * This command allows the user to specify if DC-DC or LDO is used for power regulation.
 * Using only LDO implies that the Rx or Tx current is doubled
 */
typedef enum {
    USE_LDO                                 = 0x00, // default
    USE_DCDC                                = 0x01,
} radioRegulatorMode_t;

// Represents the possible packet type (i.e. modem) used
typedef enum {
    PACKET_TYPE_GFSK                        = 0x00,
    PACKET_TYPE_LORA                        = 0x01,
    PACKET_TYPE_NONE                        = 0x0F,
} radioPacketTypes_t;

// Represents the ramping time for power amplifier
typedef enum {
    RADIO_RAMP_10_US                        = 0x00,
    RADIO_RAMP_20_US                        = 0x01,
    RADIO_RAMP_40_US                        = 0x02,
    RADIO_RAMP_80_US                        = 0x03,
    RADIO_RAMP_200_US                       = 0x04,
    RADIO_RAMP_800_US                       = 0x05,
    RADIO_RAMP_1700_US                      = 0x06,
    RADIO_RAMP_3400_US                      = 0x07,
} radioRampTimes_t;

// Represents the number of symbols to be used for channel activity detection operation
typedef enum {
    LORA_CAD_01_SYMBOL                      = 0x00,
    LORA_CAD_02_SYMBOL                      = 0x01,
    LORA_CAD_04_SYMBOL                      = 0x02,
    LORA_CAD_08_SYMBOL                      = 0x03,
    LORA_CAD_16_SYMBOL                      = 0x04,
} radioLoRaCadSymbols_t;

// Represents the Channel Activity Detection actions after the CAD operation is finished
typedef enum {
    LORA_CAD_ONLY                           = 0x00,
    LORA_CAD_RX                             = 0x01,
    LORA_CAD_LBT                            = 0x10,
} radioCadExitModes_t;

// Represents the modulation shaping parameter
typedef enum {
    MOD_SHAPING_OFF                         = 0x00,
    MOD_SHAPING_G_BT_03                     = 0x08,
    MOD_SHAPING_G_BT_05                     = 0x09,
    MOD_SHAPING_G_BT_07                     = 0x0A,
    MOD_SHAPING_G_BT_1                      = 0x0B,
} radioModShapings_t;

// Represents the modulation shaping parameter
typedef enum {
    RX_BW_4800                              = 0x1F,
    RX_BW_5800                              = 0x17,
    RX_BW_7300                              = 0x0F,
    RX_BW_9700                              = 0x1E,
    RX_BW_11700                             = 0x16,
    RX_BW_14600                             = 0x0E,
    RX_BW_19500                             = 0x1D,
    RX_BW_23400                             = 0x15,
    RX_BW_29300                             = 0x0D,
    RX_BW_39000                             = 0x1C,
    RX_BW_46900                             = 0x14,
    RX_BW_58600                             = 0x0C,
    RX_BW_78200                             = 0x1B,
    RX_BW_93800                             = 0x13,
    RX_BW_117300                            = 0x0B,
    RX_BW_156200                            = 0x1A,
    RX_BW_187200                            = 0x12,
    RX_BW_234300                            = 0x0A,
    RX_BW_312000                            = 0x19,
    RX_BW_373600                            = 0x11,
    RX_BW_467000                            = 0x09,
} radioRxBandwidth_t;

// Represents the possible spreading factor values in LoRa packet types
typedef enum {
    LORA_SF5                                = 0x05,
    LORA_SF6                                = 0x06,
    LORA_SF7                                = 0x07,
    LORA_SF8                                = 0x08,
    LORA_SF9                                = 0x09,
    LORA_SF10                               = 0x0A,
    LORA_SF11                               = 0x0B,
    LORA_SF12                               = 0x0C,
} radioLoRaSpreadingFactors_t;

// Represents the bandwidth values for LoRa packet type
typedef enum {
    LORA_BW_500                             = 6,
    LORA_BW_250                             = 5,
    LORA_BW_125                             = 4,
    LORA_BW_062                             = 3,
    LORA_BW_041                             = 10,
    LORA_BW_031                             = 2,
    LORA_BW_020                             = 9,
    LORA_BW_015                             = 1,
    LORA_BW_010                             = 8,
    LORA_BW_007                             = 0,
} radioLoRaBandwidths_t;

// Represents the coding rate values for LoRa packet type
typedef enum {
    LORA_CR_4_5                             = 0x01,
    LORA_CR_4_6                             = 0x02,
    LORA_CR_4_7                             = 0x03,
    LORA_CR_4_8                             = 0x04,
} radioLoRaCodingRates_t;

// Represents the preamble length used to detect the packet on Rx side
typedef enum {
    RADIO_PREAMBLE_DETECTOR_OFF             = 0x00,         // Preamble detection length off
    RADIO_PREAMBLE_DETECTOR_08_BITS         = 0x04,         // Preamble detection length 8 bits
    RADIO_PREAMBLE_DETECTOR_16_BITS         = 0x05,         // Preamble detection length 16 bits
    RADIO_PREAMBLE_DETECTOR_24_BITS         = 0x06,         // Preamble detection length 24 bits
    RADIO_PREAMBLE_DETECTOR_32_BITS         = 0x07,         // Preamble detection length 32 bit
} radioPreambleDetection_t;

// Represents the possible combinations of SyncWord correlators activated
typedef enum {
    RADIO_ADDRESSCOMP_FILT_OFF              = 0x00,         // No correlator turned on, i.e. do not search for SyncWord
    RADIO_ADDRESSCOMP_FILT_NODE             = 0x01,
    RADIO_ADDRESSCOMP_FILT_NODE_BROAD       = 0x02,
} radioAddressComp_t;

// Radio GFSK packet length mode
typedef enum {
    RADIO_PACKET_FIXED_LENGTH               = 0x00,         // The packet is known on both sides, no header included in the packet
    RADIO_PACKET_VARIABLE_LENGTH            = 0x01,         // The packet is on variable size, header included
} radioPacketLengthModes_t;

// Represents the CRC length
typedef enum {
    RADIO_CRC_OFF                           = 0x01,         // No CRC in use
    RADIO_CRC_1_BYTES                       = 0x00,
    RADIO_CRC_2_BYTES                       = 0x02,
    RADIO_CRC_1_BYTES_INV                   = 0x04,
    RADIO_CRC_2_BYTES_INV                   = 0x06,
    RADIO_CRC_2_BYTES_IBM                   = 0xF1,
    RADIO_CRC_2_BYTES_CCIT                  = 0xF2,
} radioCrcTypes_t;

// Radio whitening mode activated or deactivated
typedef enum {
    RADIO_DC_FREE_OFF                       = 0x00,
    RADIO_DC_FREEWHITENING                  = 0x01,
} radioDcFree_t;

// Holds the Radio lengths mode for the LoRa packet type
typedef enum {
    LORA_PACKET_VARIABLE_LENGTH             = 0x00,         // The packet is on variable size, header included
    LORA_PACKET_FIXED_LENGTH                = 0x01,         // The packet is known on both sides, no header included in the packet
    LORA_PACKET_EXPLICIT                    = LORA_PACKET_VARIABLE_LENGTH,
    LORA_PACKET_IMPLICIT                    = LORA_PACKET_FIXED_LENGTH,
} radioLoRaPacketLengthsMode_t;

// Represents the CRC mode for LoRa packet type
typedef enum {
    LORA_CRC_ON                             = 0x01,         // CRC activated
    LORA_CRC_OFF                            = 0x00,         // CRC not used
} radioLoRaCrcModes_t;

// Represents the IQ mode for LoRa packet type
typedef enum {
    LORA_IQ_NORMAL                          = 0x00,
    LORA_IQ_INVERTED                        = 0x01,
} radioLoRaIQModes_t;

// Represents the voltage used to control the TCXO on/off from DIO3
typedef enum {
    TCXO_CTRL_1_6V                          = 0x00,
    TCXO_CTRL_1_7V                          = 0x01,
    TCXO_CTRL_1_8V                          = 0x02,
    TCXO_CTRL_2_2V                          = 0x03,
    TCXO_CTRL_2_4V                          = 0x04,
    TCXO_CTRL_2_7V                          = 0x05,
    TCXO_CTRL_3_0V                          = 0x06,
    TCXO_CTRL_3_3V                          = 0x07,
} radioTcxoCtrlVoltage_t;

/*
 * Represents the interruption masks available for the radio
 *
 * Note that not all these interruptions are available for all packet types
 */
typedef enum {
    IRQ_RADIO_NONE                          = 0x0000,
    IRQ_TX_DONE                             = 0x0001,
    IRQ_RX_DONE                             = 0x0002,
    IRQ_PREAMBLE_DETECTED                   = 0x0004,
    IRQ_SYNCWORD_VALID                      = 0x0008,
    IRQ_HEADER_VALID                        = 0x0010,
    IRQ_HEADER_ERROR                        = 0x0020,
    IRQ_CRC_ERROR                           = 0x0040,
    IRQ_CAD_DONE                            = 0x0080,
    IRQ_CAD_ACTIVITY_DETECTED               = 0x0100,
    IRQ_RX_TX_TIMEOUT                       = 0x0200,
    IRQ_RADIO_ALL                           = 0xFFFF,
} radioIrqMasks_t;

// Represents all possible opcode understood by the radio
typedef enum radioCommands_e {
    RADIO_GET_STATUS                        = 0xC0,
    RADIO_WRITE_REGISTER                    = 0x0D,
    RADIO_READ_REGISTER                     = 0x1D,
    RADIO_WRITE_BUFFER                      = 0x0E,
    RADIO_READ_BUFFER                       = 0x1E,
    RADIO_SET_SLEEP                         = 0x84,
    RADIO_SET_STANDBY                       = 0x80,
    RADIO_SET_FS                            = 0xC1,
    RADIO_SET_TX                            = 0x83,
    RADIO_SET_RX                            = 0x82,
    RADIO_SET_RXDUTYCYCLE                   = 0x94,
    RADIO_SET_CAD                           = 0xC5,
    RADIO_SET_TXCONTINUOUSWAVE              = 0xD1,
    RADIO_SET_TXCONTINUOUSPREAMBLE          = 0xD2,
    RADIO_SET_PACKETTYPE                    = 0x8A,
    RADIO_GET_PACKETTYPE                    = 0x11,
    RADIO_SET_RFFREQUENCY                   = 0x86,
    RADIO_SET_TXPARAMS                      = 0x8E,
    RADIO_SET_PACONFIG                      = 0x95,
    RADIO_SET_CADPARAMS                     = 0x88,
    RADIO_SET_BUFFERBASEADDRESS             = 0x8F,
    RADIO_SET_MODULATIONPARAMS              = 0x8B,
    RADIO_SET_PACKETPARAMS                  = 0x8C,
    RADIO_GET_RXBUFFERSTATUS                = 0x13,
    RADIO_GET_PACKETSTATUS                  = 0x14,
    RADIO_GET_RSSIINST                      = 0x15,
    RADIO_GET_STATS                         = 0x10,
    RADIO_RESET_STATS                       = 0x00,
    RADIO_CFG_DIOIRQ                        = 0x08,
    RADIO_GET_IRQSTATUS                     = 0x12,
    RADIO_CLR_IRQSTATUS                     = 0x02,
    RADIO_CALIBRATE                         = 0x89,
    RADIO_CALIBRATEIMAGE                    = 0x98,
    RADIO_SET_REGULATORMODE                 = 0x96,
    RADIO_GET_ERROR                         = 0x17,
    RADIO_CLR_ERROR                         = 0x07,
    RADIO_SET_TCXOMODE                      = 0x97,
    RADIO_SET_TXFALLBACKMODE                = 0x93,
    RADIO_SET_RFSWITCHMODE                  = 0x9D,
    RADIO_SET_STOPRXTIMERONPREAMBLE         = 0x9F,
    RADIO_SET_LORASYMBTIMEOUT               = 0xA0,
} radioCommands_t;

// The type describing the modulation parameters for every packet types
typedef struct {
    radioPacketTypes_t                   packetType;        // Packet to which the modulation parameters are referring to.
    struct {
        struct {
            uint32_t                     bitRate;
            uint32_t                     fdev;
            radioModShapings_t           modulationShaping;
            uint8_t                      bandwidth;
        } gfsk;

        struct {
            radioLoRaSpreadingFactors_t  spreadingFactor;     // Spreading Factor for the LoRa modulation
            radioLoRaBandwidths_t        bandwidth;           // Bandwidth for the LoRa modulation
            radioLoRaCodingRates_t       codingRate;          // Coding rate for the LoRa modulation
            uint8_t                      lowDatarateOptimize; // Indicates if the modem uses the low datarate optimization
        } loRa;
    } params;                                                 // Holds the modulation parameters structure
} modulationParams_t;

// The type describing the packet parameters for every packet types
typedef struct {
    radioPacketTypes_t                   packetType;        // Packet to which the packet parameters are referring to.
    struct {
        // Holds the GFSK packet parameters
        struct {
            uint16_t                     preambleLength;    // The preamble Tx length for GFSK packet type in bit
            radioPreambleDetection_t     preambleMinDetect; // The preamble Rx length minimal for GFSK packet type
            uint8_t                      syncWordLength;    // The synchronization word length for GFSK packet type
            radioAddressComp_t           addrComp;          // Activated SyncWord correlators
            radioPacketLengthModes_t     headerType;        // If the header is explicit, it will be transmitted in the GFSK packet. If the header is implicit, it will not be transmitted
            uint8_t                      payloadLength;     // Size of the payload in the GFSK packet
            radioCrcTypes_t              crcLength;         // Size of the CRC block in the GFSK packet
            radioDcFree_t                dcFree;
        } gfsk;

        // Holds the LoRa packet parameters
        struct {
            uint16_t                     preambleLength;    // The preamble length is the number of LoRa symbols in the preamble
            radioLoRaPacketLengthsMode_t headerType;        // If the header is explicit, it will be transmitted in the LoRa packet. If the header is implicit, it will not be transmitted
            uint8_t                      payloadLength;     // Size of the payload in the LoRa packet
            radioLoRaCrcModes_t          crcMode;           // Size of CRC block in LoRa packet
            radioLoRaIQModes_t           invertIQ;          // Allows to swap IQ for LoRa packet
        } loRa;
    } params;                                               // Holds the packet parameters structure
} packetParams_t;

// Represents the packet status for every packet type
typedef struct {
    radioPacketTypes_t                   packetType;        // Packet to which the packet status are referring to.
    struct {
        struct {
            uint8_t 					 rxStatus;
            int8_t 						 rssiAvg;			// The averaged RSSI
            int8_t 						 rssiSync;			// The RSSI measured on last packet
            uint32_t 					 freqError;
        } gfsk;

        struct {
            int8_t 						 rssiPkt;			// The RSSI of the last packet
            int8_t 						 snrPkt;            // The SNR of the last packet
            int8_t 						 signalRssiPkt;
            uint32_t 					 freqError;
        } loRa;
    } params;
} packetStatus_t;

// Represents the Rx internal counters values when GFSK or LoRa packet type is used
typedef struct {
    radioPacketTypes_t                   packetType; 		// Packet to which the packet status are referring to.
    uint16_t 							 packetReceived;
    uint16_t 							 vrcOk;
    uint16_t 							 lengthError;
} rxCounter_t;

// Represents a calibration configuration
typedef union {
    struct {
        uint8_t rc64kEnable    : 1;                             // Calibrate RC64K clock
        uint8_t rc13mEnable    : 1;                             // Calibrate RC13M clock
        uint8_t pllEnable      : 1;                             // Calibrate PLL
        uint8_t adcPulseEnable : 1;                             // Calibrate ADC Pulse
        uint8_t adcBulkNEnable : 1;                             // Calibrate ADC bulkN
        uint8_t adcBulkPEnable : 1;                             // Calibrate ADC bulkP
        uint8_t imgEnable      : 1;
        uint8_t                : 1;
    } fields;
    uint8_t value;
} calibrationParams_t;

// Represents a sleep mode configuration
typedef union {
    struct {
        uint8_t wakeUpRTC	: 1; // Get out of sleep mode if wakeup signal received from RTC
        uint8_t reset		: 1;
        uint8_t warmStart	: 1;
        uint8_t reserved 	: 5;
    } fields;
    uint8_t value;
} sleepParams_t;

// Represents the possible radio system error states
typedef union {
    struct {
        uint8_t rc64kCalib	: 1; // RC 64kHz oscillator calibration failed
        uint8_t rc13mCalib	: 1; // RC 13MHz oscillator calibration failed
        uint8_t pllCalib 	: 1; // PLL calibration failed
        uint8_t adcCalib  	: 1; // ADC calibration failed
        uint8_t imgCalib  	: 1; // Image calibration failed
        uint8_t xoscStart  	: 1; // XOSC oscillator failed to start
        uint8_t pllLock   	: 1; // PLL lock failed
        uint8_t buckStart 	: 1; // Buck converter failed to start
        uint8_t paRamp    	: 1; // PA ramp failed
        uint8_t           	: 7; // Reserved
    } fields;
    uint16_t value;
} radioError_t;

// TODO: move in utils.h
typedef struct {
    GPIO_TypeDef 	*port;
    uint16_t 		pin;
} gpioPin_t;

// Radio hardware and global parameters
typedef struct sx1268_s {
	SPI_HandleTypeDef   *hspi;
	gpioPin_t			reset_gpio;
	gpioPin_t			busy_gpio;
	gpioPin_t			dio1_gpio;
	gpioPin_t			dio2_gpio;

    packetParams_t 		packetParams;
    packetStatus_t 		packetStatus;
    modulationParams_t 	modulationParams;
} sx1268_t;

// Hardware IO IRQ callback function definition
typedef void (DioIrqHandler)(void);

/*
 * ============================================================================
 * SX1268 definitions
 * ============================================================================
 */

/*
 * Provides the frequency of the chip running on the radio and the frequency step
 *
 * These defines are used for computing the frequency divider to set the RF frequency
 */
#define XTAL_FREQ			(double)32000000
#define FREQ_DIV			(double)pow(2.0, 25.0)
#define FREQ_STEP			(double)(XTAL_FREQ / FREQ_DIV)

#define RX_BUFFER_SIZE		256

// Callback function definition
typedef void (*CallbackFunction)(void);

/*
 * The radio callbacks structure
 * Holds function pointers to be called on radio interrupts
 */
typedef struct {
	CallbackFunction txDone;                       // Pointer to a function run on successful transmission
	CallbackFunction rxDone;                       // Pointer to a function run on successful reception
	CallbackFunction rxPreambleDetect;             // Pointer to a function run on successful Preamble detection
	CallbackFunction rxSyncWordDone;               // Pointer to a function run on successful SyncWord reception
    void (*rxHeaderDone)(bool isOk);               // Pointer to a function run on successful Header reception
    CallbackFunction txTimeout;                    // Pointer to a function run on transmission timeout
    CallbackFunction rxTimeout;                    // Pointer to a function run on reception timeout
    void (*rxError)(irqErrorCode_t errCode);       // Pointer to a function run on reception error
    void (*cadDone)(bool cadFlag);                 // Pointer to a function run on channel activity detected
} sx1268Callbacks_t;

/*
 * ============================================================================
 * Public functions prototypes
 * ============================================================================
 */

// TODO: add uint8_t return enum of public functions

// Initializes the radio driver
void SX1268_Init(DioIrqHandler dioIrq);

// TODO: todo....
// Reset the radio parameters
void SX1268_Reset(void);

/*
 * Gets the current Operation Mode of the Radio
 *
 * [return]  RadioOperatingModes_t last operating mode
 */
radioOperatingModes_t SX1268_GetOperatingMode(void);

// Wakeup the radio if it is in Sleep mode and check that Busy is low
void SX1268_CheckDeviceReady(void);

/*
 * Saves the payload to be send in the radio buffer
 *
 * [in]  payload       A pointer to the payload
 * [in]  size          The size of the payload
 */
void SX1268_SetPayload(uint8_t *payload, uint8_t size);

/*
 * Reads the payload received. If the received payload is longer than maxSize, then the method returns 1 and do not set size and payload.
 *
 * [out] payload       A pointer to a buffer into which the payload will be copied
 * [out] size          A pointer to the size of the payload received
 * [in]  maxSize       The maximal size allowed to copy into the buffer
 */
uint8_t SX1268_GetPayload(uint8_t *payload, uint8_t *size, uint8_t maxSize);

/*
 * Sends a payload
 *
 * [in]  payload       A pointer to the payload to send
 * [in]  size          The size of the payload to send
 * [in]  timeout       The timeout for Tx operation
 */
void SX1268_SendPayload(uint8_t *payload, uint8_t size, uint32_t timeout);

/*
 * Sets the Sync Word given by index used in GFSK
 *
 * [in]  syncWord      SyncWord bytes ( 8 bytes )
 *
 * status        	   [0: OK, 1: NOK]
 */
uint8_t SX1268_SetSyncWord(uint8_t *syncWord);

/*
 * Sets the Initial value for the LFSR used for the CRC calculation
 *
 * [in]  seed          Initial LFSR value ( 2 bytes )
 *
 */
void SX1268_SetCrcSeed(uint16_t seed);

/*
 * Sets the seed used for the CRC calculation
 *
 * [in]  seed          The seed value
 *
 */
void SX1268_SetCrcPolynomial(uint16_t polynomial);

/*
 * Sets the Initial value of the LFSR used for the whitening in GFSK protocols
 *
 * [in]  seed          Initial LFSR value
 */
void SX1268_SetWhiteningSeed(uint16_t seed);

/*
 * Gets a 32 bits random value generated by the radio
 *
 * The radio must be in reception mode before executing this function
 *
 * [return]  randomValue    32 bits random value
 */
uint32_t SX1268_GetRandom(void);

/*
 * Sets the radio in sleep mode
 *
 * [in]  sleepConfig   The sleep configuration describing data retention and RTC wake-up
 */
void SX1268_SetSleep(sleepParams_t sleepConfig);

/*
 * Sets the radio in configuration mode
 *
 * [in]  mode          The standby mode to put the radio into
 */
void SX1268_SetStandby(radioStandbyModes_t mode);

/*
 * Sets the radio in FS mode
 */
void SX1268_SetFs(void);

/*
 * Sets the radio in transmission mode
 *
 * [in]  timeout       Structure describing the transmission timeout value
 */
void SX1268_SetTx(uint32_t timeout);

/*
 * Sets the radio in reception mode
 *
 * [in]  timeout       Structure describing the reception timeout value
 */
void SX1268_SetRx(uint32_t timeout);

/*
 * Sets the radio in reception mode with Boosted LNA gain
 *
 * [in]  timeout       Structure describing the reception timeout value
 */
void SX1268_SetRxBoosted(uint32_t timeout);

/*
 * Sets the Rx duty cycle management parameters
 *
 * [in]  rxTime        Structure describing reception timeout value
 * [in]  sleepTime     Structure describing sleep timeout value
 */
void SX1268_SetRxDutyCycle(uint32_t rxTime, uint32_t sleepTime);

// Sets the radio in CAD mode
void SX1268_SetCad(void);

// Sets the radio in continuous wave transmission mode
void SX1268_SetTxContinuousWave(void);

// Sets the radio in continuous preamble transmission mode
void SX1268_SetTxInfinitePreamble(void);

/*
 * Decide which interrupt will stop the internal radio rx timer
 *
 * [in]  enable          [0: Timer stop after header/syncword detection | 1: Timer stop after preamble detection]
 */
void SX1268_SetStopRxTimerOnPreambleDetect(bool enable);

/*
 * Set the number of symbol the radio will wait to validate a reception
 *
 * [in]  SymbNum          number of LoRa symbols
 */
void SX1268_SetLoRaSymbNumTimeout(uint8_t symbNum);

/*
 * Sets the power regulators operating mode
 *
 * [in]  mode          [0: LDO, 1:DC_DC]
 */
void SX1268_SetRegulatorMode(radioRegulatorMode_t mode);

/*
 * Calibrates the given radio block
 *
 * [in]  calibParam    The description of blocks to be calibrated
 */
void SX1268_Calibrate(calibrationParams_t calibParam);

/*
 * Calibrates the Image rejection depending of the frequency
 *
 * [in]  freq    The operating frequency
 */
void SX1268_CalibrateImage(uint32_t freq);

/*
 * Activate the extention of the timeout when long preamble is used
 *
 * [in]  enable      The radio will extend the timeout to cope with long preamble
 */
void SX1268_SetLongPreamble(uint8_t enable);

// TODO: adjust this for sx1268
/*
 * Sets the transmission parameters for power amplifier
 *
 * [in]  paDutyCycle     Duty Cycle for the internal clock
 * [in]  hpMax			 Size of internal PA
 *
 * DS_SX1268_V1.0 		p74 -> optimal configuration
 */
void SX1268_SetPaConfig(uint8_t paDutyCycle, uint8_t hpMax);

/*
 * Defines into which mode the chip goes after a TX / RX done
 *
 * [in]  fallbackMode    The mode in which the radio goes
 */
void SX1268_SetRxTxFallbackMode(uint8_t fallbackMode);

/*
 * Write data to the radio memory
 *
 * [in]  address       The address of the first byte to write in the radio
 * [in]  buffer        The data to be written in radio's memory
 * [in]  size          The number of bytes to write in radio's memory
 */
void SX1268_WriteRegisters(uint16_t address, uint8_t *buffer, uint16_t size);

/*
 * Read data from the radio memory
 *
 * [in]  address       The address of the first byte to read from the radio
 * [out] buffer        The buffer that holds data read from radio
 * [in]  size          The number of bytes to read from radio's memory
 */
void SX1268_ReadRegisters(uint16_t address, uint8_t *buffer, uint16_t size);

/*
 * Write data to the buffer holding the payload in the radio
 *
 * [in]  offset        The offset to start writing the payload
 * [in]  buffer        The data to be written (the payload)
 * [in]  size          The number of byte to be written
 */
void SX1268_WriteBuffer(uint8_t offset, uint8_t *buffer, uint8_t size);

/*
 * Read data from the buffer holding the payload in the radio
 *
 * [in]  offset        The offset to start reading the payload
 * [out] buffer        A pointer to a buffer holding the data from the radio
 * [in]  size          The number of byte to be read
 */
void SX1268_ReadBuffer(uint8_t offset, uint8_t *buffer, uint8_t size);

/*
 * Sets the IRQ mask and DIO masks
 *
 * [in]  irqMask       General IRQ mask
 * [in]  dio1Mask      DIO1 mask
 * [in]  dio2Mask      DIO2 mask
 * [in]  dio3Mask      DIO3 mask
 */
void SX1268_SetDioIrqParams(uint16_t irqMask, uint16_t dio1Mask, uint16_t dio2Mask, uint16_t dio3Mask);

/*
 * Returns the current IRQ status
 *
 * [return]  irqStatus     IRQ status
 */
uint16_t SX1268_GetIrqStatus(void);

// TODO: Need to be true for my configuration
/*
 * Indicates if DIO2 is used to control an RF Switch (TXEN pin) for auto TX mode
 *
 * [in] enable     true of false
 */
void SX1268_SetDio2AsRfSwitchCtrl(uint8_t enable);

/*
 * Indicates if the Radio main clock is supplied from a tcxo
 *
 * [in] tcxoVoltage     voltage used to control the TCXO
 * [in] timeout         time given to the TCXO to go to 32MHz
 */
void SX1268_SetDio3AsTcxoCtrl(radioTcxoCtrlVoltage_t tcxoVoltage, uint32_t timeout);

/*
 * Sets the RF frequency
 *
 * [in]  frequency     RF frequency [Hz]
 */
void SX1268_SetRfFrequency(uint32_t frequency);

/*
 * Sets the radio for the given protocol
 *
 * [in]  packetType    [PACKET_TYPE_GFSK, PACKET_TYPE_LORA]
 *
 * This method has to be called before SetRfFrequency, SetModulationParams and SetPacketParams
 */
void SX1268_SetPacketType(radioPacketTypes_t packetType);

/*
 * Gets the current radio protocol
 *
 * [return]  packetType    [PACKET_TYPE_GFSK, PACKET_TYPE_LORA]
 */
radioPacketTypes_t SX1268_GetPacketType(void);

// TODO: adjust this for sx1268
/*
 * Sets the transmission parameters
 *
 * [in]  power         RF output power [-9..22] dBm (10-14)
 * [in]  rampTime      Transmission ramp up time
 */
void SX1268_SetTxParams(int8_t power, radioRampTimes_t rampTime);

/*
 * Set the modulation parameters
 *
 * [in]  modParams     A structure describing the modulation parameters
 */
void SX1268_SetModulationParams(modulationParams_t *modParams);

/*
 * Sets the packet parameters
 *
 * [in]  packetParams  A structure describing the packet parameters
 */
void SX1268_SetPacketParams(packetParams_t *packetParams);

/*
 * Sets the Channel Activity Detection (CAD) parameters
 *
 * [in]  cadSymbolNum   The number of symbol to use for CAD operations [LORA_CAD_01_SYMBOL, LORA_CAD_02_SYMBOL, LORA_CAD_04_SYMBOL, LORA_CAD_08_SYMBOL, LORA_CAD_16_SYMBOL]
 * [in]  cadDetPeak     Limit for detection of SNR peak used in the CAD
 * [in]  cadDetMin      Set the minimum symbol recognition for CAD
 * [in]  cadExitMode    Operation to be done at the end of CAD action [LORA_CAD_ONLY, LORA_CAD_RX, LORA_CAD_LBT]
 * [in]  cadTimeout     Defines the timeout value to abort the CAD activity
 */
void SX1268_SetCadParams(radioLoRaCadSymbols_t cadSymbolNum, uint8_t cadDetPeak, uint8_t cadDetMin, radioCadExitModes_t cadExitMode, uint32_t cadTimeout);

/*
 * Sets the data buffer base address for transmission and reception
 *
 * [in]  txBaseAddress Transmission base address
 * [in]  rxBaseAddress Reception base address
 */
void SX1268_SetBufferBaseAddress(uint8_t txBaseAddress, uint8_t rxBaseAddress);

/*
 * Gets the current radio status
 *
 * [return]  status        Radio status
 */
radioStatus_t SX1268_GetStatus(void);

/*
 * Returns the instantaneous RSSI value for the last packet received
 *
 * [return]  rssiInst      Instantaneous RSSI
 */
int8_t SX1268_GetRssiInst(void);

/*
 * Gets the last received packet buffer status
 *
 * [out] payloadLength Last received packet payload length
 * [out] rxStartBuffer Last received packet buffer address pointer
 */
void SX1268_GetRxBufferStatus(uint8_t *payloadLength, uint8_t *rxStartBuffer);

/*
 * Gets the last received packet payload length
 *
 * [out] pktStatus     A structure of packet status
 */
void SX1268_GetPacketStatus(packetStatus_t *pktStatus);

/*
 * Returns the possible system errors
 *
 * [return]  sysErrors Value representing the possible sys failures
 */
radioError_t SX1268_GetDeviceErrors(void);

// Clear all the errors in the device
void SX1268_ClearDeviceErrors(void);

/*
 * Clears the IRQs
 *
 * [in]  irq           IRQ(s) to be cleared
 */
void SX1268_ClearIrqStatus(uint16_t irq);


#endif /* INC_DRIVERS_SX1268_H_ */
