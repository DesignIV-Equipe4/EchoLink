/*
 * sx1268.c
 *
 *  Created on: 8 sept. 2026
 *      Author: gagno
 */


#include "Drivers/sx1268.h"
//#include "sx126x-board.h"
#include <math.h>
#include <string.h>

// Radio registers definition
typedef struct {
    uint16_t      addr;                             // The address of the register
    uint8_t       value;                            // The value of the register
} radioRegisters_t;

// Holds the internal operating mode of the radio
static radioOperatingModes_t radioOperatingMode;
// Stores the current packet type set in the radio
static radioPacketTypes_t radioPacketType;
// Stores the last frequency error measured on LoRa received packet
volatile uint32_t frequencyError = 0;
// Hold the status of the Image calibration
static bool imageCalibrated = false;

/*
 * ============================================================================
 * SX1268 DIO IRQ callback functions prototype
 * ============================================================================
 */

// DIO 0 IRQ callback
void SX1268_OnDioIrq(void);
// DIO 0 IRQ callback
void SX1268_SetPollingMode(void);
// DIO 0 IRQ callback
void SX1268_SetInterruptMode(void);
// Process the IRQ if handled by the driver
void SX1268_ProcessIrqs(void);


void SX1268_Init(DioIrqHandler dioIrq) {
    SX1268_Reset();

    SX1268_IoIrqInit(dioIrq);

    SX1268_Wakeup();
    SX1268_SetStandby(STDBY_RC);

    // TODO: verify that...
/*
#ifdef USE_TCXO
    CalibrationParams_t calibParam;

    SX1268_SetDio3AsTcxoCtrl( TCXO_CTRL_1_7V, RADIO_TCXO_SETUP_TIME << 6 ); // convert from ms to SX1268 time base
    calibParam.Value = 0x7F;
    SX1268_Calibrate( calibParam );
#endif
*/

    SX1268_SetDio2AsRfSwitchCtrl(true);
    radioOperatingMode = MODE_STDBY_RC;
}

radioOperatingModes_t SX1268_GetOperatingMode(void) {
    return radioOperatingMode;
}

void SX1268_CheckDeviceReady(void) {
    if((SX1268_GetOperatingMode() == MODE_SLEEP) || (SX1268_GetOperatingMode() == MODE_RX_DC )) {
        SX1268_Wakeup();
        // Switch is turned off when device is in sleep mode and turned on is all other modes
        SX1268_AntSwOn();
    }

    SX1268_WaitOnBusy();
}

void SX1268_SetPayload(uint8_t *payload, uint8_t size) {
    SX1268_WriteBuffer(0x00, payload, size);
}

uint8_t SX1268_GetPayload(uint8_t *buffer, uint8_t *size,  uint8_t maxSize) {
    uint8_t offset = 0;
    SX1268_GetRxBufferStatus(size, &offset);
    if(*size > maxSize) {
        return 1;
    }

    SX1268_ReadBuffer(offset, buffer, *size);

    return 0;
}

void SX1268_SendPayload(uint8_t *payload, uint8_t size, uint32_t timeout) {
    SX1268_SetPayload( payload, size );
    SX1268_SetTx( timeout );
}

uint8_t SX1268_SetSyncWord( uint8_t *syncWord ) {
    SX1268_WriteRegisters(REG_LR_SYNCWORDBASEADDRESS, syncWord, 8);
    return 0;
}

void SX1268_SetCrcSeed(uint16_t seed) {
    uint8_t buf[2];
    buf[0] = (uint8_t)((seed >> 8) & 0xFF);
    buf[1] = (uint8_t)(seed & 0xFF);

    switch(SX1268_GetPacketType()) {
        case PACKET_TYPE_GFSK:
            SX1268_WriteRegisters(REG_LR_CRCSEEDBASEADDR, buf, 2);
            break;

        default:
            break;
    }
}

void SX1268_SetCrcPolynomial(uint16_t polynomial) {
    uint8_t buf[2];
    buf[0] = (uint8_t)((polynomial >> 8) & 0xFF);
    buf[1] = (uint8_t)(polynomial & 0xFF);

    switch(SX1268_GetPacketType()) {
        case PACKET_TYPE_GFSK:
            SX1268_WriteRegisters(REG_LR_CRCPOLYBASEADDR, buf, 2);
            break;

        default:
            break;
    }
}

void SX1268_SetWhiteningSeed(uint16_t seed){
    uint8_t regValue = 0;

    switch(SX1268_GetPacketType()) {
        case PACKET_TYPE_GFSK:
            regValue = SX1268_ReadRegister(REG_LR_WHITSEEDBASEADDR_MSB) & 0xFE;
            regValue = ((seed >> 8) & 0x01) | regValue;
            SX1268_WriteRegister(REG_LR_WHITSEEDBASEADDR_MSB, regValue); // only 1 bit.
            SX1268_WriteRegister(REG_LR_WHITSEEDBASEADDR_LSB, (uint8_t)seed);
            break;

        default:
            break;
    }
}

uint32_t SX1268_GetRandom(void) {
    uint8_t buf[] = {0, 0, 0, 0};

    // Set radio in continuous reception
    SX1268_SetRx(0);

    DelayMs(1);

    SX1268_ReadRegisters(RANDOM_NUMBER_GENERATORBASEADDR, buf, 4);

    SX1268_SetStandby(STDBY_RC);

    return (buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | buf[3];
}

void SX1268_SetSleep(sleepParams_t sleepConfig) {
    SX1268_AntSwOff();

    SX1268_WriteCommand(RADIO_SET_SLEEP, &sleepConfig.value, 1);
    radioOperatingMode = MODE_SLEEP;
}

void SX1268_SetStandby(radioStandbyModes_t standbyConfig) {
    SX1268_WriteCommand(RADIO_SET_STANDBY, (uint8_t*)&standbyConfig, 1);
    if(standbyConfig == STDBY_RC) {
        radioOperatingMode = MODE_STDBY_RC;
    } else {
    	radioOperatingMode = MODE_STDBY_XOSC;
    }
}

void SX1268_SetFs(void) {
    SX1268_WriteCommand(RADIO_SET_FS, 0, 0);
    radioOperatingMode = MODE_FS;
}

void SX1268_SetTx(uint32_t timeout) {
    uint8_t buf[3];

    radioOperatingMode = MODE_TX;

    buf[0] = (uint8_t)((timeout >> 16) & 0xFF);
    buf[1] = (uint8_t)((timeout >> 8) & 0xFF);
    buf[2] = (uint8_t)(timeout & 0xFF);
    SX1268_WriteCommand(RADIO_SET_TX, buf, 3);
}

void SX1268_SetRx(uint32_t timeout) {
    uint8_t buf[3];

    radioOperatingMode = MODE_RX;

    buf[0] = (uint8_t)((timeout >> 16) & 0xFF);
    buf[1] = (uint8_t)((timeout >> 8)  & 0xFF);
    buf[2] = (uint8_t)(timeout & 0xFF);
    SX1268_WriteCommand(RADIO_SET_RX, buf, 3);
}

void SX1268_SetRxBoosted(uint32_t timeout) {
    uint8_t buf[3];

    radioOperatingMode = MODE_RX;

    SX1268_WriteRegister(REG_RX_GAIN, 0x96); // max LNA gain, increase current by ~2mA for around ~3dB in sensivity

    buf[0] = (uint8_t)((timeout >> 16) & 0xFF);
    buf[1] = (uint8_t)((timeout >> 8) & 0xFF);
    buf[2] = (uint8_t)(timeout & 0xFF);
    SX1268_WriteCommand(RADIO_SET_RX, buf, 3);
}

void SX1268_SetRxDutyCycle(uint32_t rxTime, uint32_t sleepTime) {
    uint8_t buf[6];
    buf[0] = (uint8_t)((rxTime >> 16) & 0xFF);
    buf[1] = (uint8_t)((rxTime >> 8) & 0xFF);
    buf[2] = (uint8_t)(rxTime & 0xFF);
    buf[3] = (uint8_t)((sleepTime >> 16) & 0xFF);
    buf[4] = (uint8_t)((sleepTime >> 8) & 0xFF);
    buf[5] = (uint8_t)(sleepTime & 0xFF);
    SX1268_WriteCommand(RADIO_SET_RXDUTYCYCLE, buf, 6);

    radioOperatingMode = MODE_RX_DC;
}

void SX1268_SetCad(void) {
    SX1268_WriteCommand(RADIO_SET_CAD, 0, 0);
    radioOperatingMode = MODE_CAD;
}

void SX1268_SetTxContinuousWave(void) {
    SX1268_WriteCommand(RADIO_SET_TXCONTINUOUSWAVE, 0, 0);
}

void SX1268_SetTxInfinitePreamble(void) {
    SX1268_WriteCommand(RADIO_SET_TXCONTINUOUSPREAMBLE, 0, 0);
}

void SX1268_SetStopRxTimerOnPreambleDetect(bool enable) {
    SX1268_WriteCommand(RADIO_SET_STOPRXTIMERONPREAMBLE, (uint8_t*)&enable, 1);
}

void SX1268_SetLoRaSymbNumTimeout(uint8_t symbNum) {
    SX1268_WriteCommand(RADIO_SET_LORASYMBTIMEOUT, &symbNum, 1);
}

void SX1268_SetRegulatorMode(radioRegulatorMode_t mode) {
    SX1268_WriteCommand(RADIO_SET_REGULATORMODE, (uint8_t*)&mode, 1);
}

void SX1268_Calibrate(calibrationParams_t calibParam) {
    SX1268_WriteCommand(RADIO_CALIBRATE, (uint8_t*)&calibParam, 1);
}

void SX1268_CalibrateImage(uint32_t freq) {
    uint8_t calFreq[2];
    if(freq >= 470000000) {
        calFreq[0] = 0x75;
        calFreq[1] = 0x81;
    } else if(freq >= 430000000) {
        calFreq[0] = 0x6B;
        calFreq[1] = 0x6F;
    }

    // TODO: Missing < 430MHz ...

    SX1268_WriteCommand(RADIO_CALIBRATEIMAGE, calFreq, 2);
}

void SX1268_SetPaConfig(uint8_t paDutyCycle, uint8_t hpMax) {
    uint8_t buf[4];
    buf[0] = paDutyCycle;
    buf[1] = hpMax;
    buf[2] = 0x00;
    buf[3] = 0x01;
    SX1268_WriteCommand(RADIO_SET_PACONFIG, buf, 4);
}

void SX1268_SetRxTxFallbackMode(uint8_t fallbackMode) {
    SX1268_WriteCommand(RADIO_SET_TXFALLBACKMODE, &fallbackMode, 1);
}

void SX1268_SetDioIrqParams(uint16_t irqMask, uint16_t dio1Mask, uint16_t dio2Mask, uint16_t dio3Mask) {
    uint8_t buf[8];
    buf[0] = (uint8_t)((irqMask >> 8) & 0x00FF);
    buf[1] = (uint8_t)(irqMask & 0x00FF);
    buf[2] = (uint8_t)((dio1Mask >> 8) & 0x00FF);
    buf[3] = (uint8_t)(dio1Mask & 0x00FF);
    buf[4] = (uint8_t)((dio2Mask >> 8) & 0x00FF);
    buf[5] = (uint8_t)(dio2Mask & 0x00FF);
    buf[6] = (uint8_t)((dio3Mask >> 8) & 0x00FF);
    buf[7] = (uint8_t)(dio3Mask & 0x00FF);
    SX1268_WriteCommand(RADIO_CFG_DIOIRQ, buf, 8);
}

uint16_t SX1268_GetIrqStatus(void) {
    uint8_t irqStatus[2];
    SX1268_ReadCommand( RADIO_GET_IRQSTATUS, irqStatus, 2 );

    return (irqStatus[0] << 8) | irqStatus[1];
}

void SX1268_SetDio2AsRfSwitchCtrl(uint8_t enable) {
    SX1268_WriteCommand(RADIO_SET_RFSWITCHMODE, &enable, 1);
}

void SX1268_SetDio3AsTcxoCtrl(radioTcxoCtrlVoltage_t tcxoVoltage, uint32_t timeout) {
    uint8_t buf[4];
    buf[0] = tcxoVoltage & 0x07;
    buf[1] = (uint8_t)((timeout >> 16) & 0xFF);
    buf[2] = (uint8_t)((timeout >> 8) & 0xFF);
    buf[3] = (uint8_t)(timeout & 0xFF);

    SX1268_WriteCommand(RADIO_SET_TCXOMODE, buf, 4);
}

// TODO: adjust this for sx1268
void SX1268_SetRfFrequency(uint32_t frequency) {
    uint8_t buf[4];
    uint32_t freq = 0;

    if(imageCalibrated == false) {
        SX1268_CalibrateImage(frequency);
        imageCalibrated = true;
    }

    freq = (uint32_t)((double)frequency / (double)FREQ_STEP);
    buf[0] = (uint8_t)((freq >> 24) & 0xFF);
    buf[1] = (uint8_t)((freq >> 16) & 0xFF);
    buf[2] = (uint8_t)((freq >> 8) & 0xFF);
    buf[3] = (uint8_t)(freq & 0xFF);
    SX1268_WriteCommand(RADIO_SET_RFFREQUENCY, buf, 4);
}

void SX1268_SetPacketType(radioPacketTypes_t packetType) {
    // Save packet type internally to avoid questioning the radio
    radioPacketType = packetType;
    SX1268_WriteCommand(RADIO_SET_PACKETTYPE, (uint8_t*)&packetType, 1);
}

radioPacketTypes_t SX1268_GetPacketType(void) {
    return radioPacketType;
}

// TODO: measure current to test that
void SX1268_SetTxParams(int8_t power, radioRampTimes_t rampTime) {
    uint8_t buf[2];

    if(power >= 14) {
		power = 14;
	} else if(power <= 10) {
		power = 10;
	}

    if(power == 10) {
		SX1268_SetPaConfig(0x00, 0x00);
	} else {
		// +14 dBm
		SX1268_SetPaConfig(0x02, 0x02);
	}

	SX1268_WriteRegister(REG_OCP, 0x18);

    buf[0] = power;
    buf[1] = (uint8_t)rampTime;
    SX1268_WriteCommand(RADIO_SET_TXPARAMS, buf, 2);
}

void SX1268_SetModulationParams(modulationParams_t *modulationParams) {
    uint8_t n;
    uint32_t tempVal = 0;
    uint8_t buf[8] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

    // Check if required configuration corresponds to the stored packet type
    // If not, silently update radio packet type
    if(radioPacketType != modulationParams->packetType ) {
        SX1268_SetPacketType(modulationParams->packetType);
    }

    switch(modulationParams->packetType) {
		case PACKET_TYPE_GFSK:
			n = 8;
			tempVal = (uint32_t)(32 * ((double)XTAL_FREQ / (double)modulationParams->params.gfsk.bitRate));
			buf[0] = (tempVal >> 16) & 0xFF;
			buf[1] = (tempVal >> 8) & 0xFF;
			buf[2] = tempVal & 0xFF;
			buf[3] = modulationParams->params.gfsk.modulationShaping;
			buf[4] = modulationParams->params.gfsk.bandwidth;
			tempVal = (uint32_t)((double)modulationParams->params.gfsk.fdev / (double)FREQ_STEP);
			buf[5] = (tempVal >> 16) & 0xFF;
			buf[6] = (tempVal >> 8) & 0xFF;
			buf[7] = (tempVal & 0xFF);
			SX1268_WriteCommand(RADIO_SET_MODULATIONPARAMS, buf, n);
			break;

		case PACKET_TYPE_LORA:
			n = 4;
			buf[0] = modulationParams->params.loRa.spreadingFactor;
			buf[1] = modulationParams->params.loRa.bandwidth;
			buf[2] = modulationParams->params.loRa.codingRate;
			buf[3] = modulationParams->params.loRa.lowDatarateOptimize;

			SX1268_WriteCommand(RADIO_SET_MODULATIONPARAMS, buf, n);
			break;

		default:
		case PACKET_TYPE_NONE:
			return;
    }
}

void SX1268_SetPacketParams(packetParams_t *packetParams) {
    uint8_t n;
    uint8_t crcVal = 0;
    uint8_t buf[9] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

    // Check if required configuration corresponds to the stored packet type
    // If not, silently update radio packet type
    if(radioPacketType != packetParams->packetType) {
        SX1268_SetPacketType(packetParams->packetType);
    }

    switch(packetParams->packetType) {
		case PACKET_TYPE_GFSK:
			if(packetParams->params.gfsk.crcLength == RADIO_CRC_2_BYTES_IBM)
			{
				SX1268_SetCrcSeed(CRC_IBM_SEED);
				SX1268_SetCrcPolynomial(CRC_POLYNOMIAL_IBM);
				crcVal = RADIO_CRC_2_BYTES;
			} else if(packetParams->params.gfsk.crcLength == RADIO_CRC_2_BYTES_CCIT) {
				SX1268_SetCrcSeed(CRC_CCITT_SEED);
				SX1268_SetCrcPolynomial(CRC_POLYNOMIAL_CCITT);
				crcVal = RADIO_CRC_2_BYTES_INV;
			} else {
				crcVal = packetParams->params.gfsk.crcLength;
			}
			n = 9;
			buf[0] = (packetParams->params.gfsk.preambleLength >> 8) & 0xFF;
			buf[1] = packetParams->params.gfsk.preambleLength;
			buf[2] = packetParams->params.gfsk.preambleMinDetect;
			buf[3] = (packetParams->params.gfsk.syncWordLength /*<< 3*/); // convert from byte to bit
			buf[4] = packetParams->params.gfsk.addrComp;
			buf[5] = packetParams->params.gfsk.headerType;
			buf[6] = packetParams->params.gfsk.payloadLength;
			buf[7] = crcVal;
			buf[8] = packetParams->params.gfsk.dcFree;
			break;

		case PACKET_TYPE_LORA:
			n = 6;
			buf[0] = (packetParams->params.loRa.preambleLength >> 8) & 0xFF;
			buf[1] = packetParams->params.loRa.preambleLength;
			buf[2] = packetParams->params.loRa.headerType;
			buf[3] = packetParams->params.loRa.payloadLength;
			buf[4] = packetParams->params.loRa.crcMode;
			buf[5] = packetParams->params.loRa.invertIQ;
			break;

		default:
		case PACKET_TYPE_NONE:
			return;
	}

    SX1268_WriteCommand(RADIO_SET_PACKETPARAMS, buf, n);
}

void SX1268_SetCadParams(radioLoRaCadSymbols_t cadSymbolNum, uint8_t cadDetPeak, uint8_t cadDetMin, radioCadExitModes_t cadExitMode, uint32_t cadTimeout) {
    uint8_t buf[7];
    buf[0] = (uint8_t)cadSymbolNum;
    buf[1] = cadDetPeak;
    buf[2] = cadDetMin;
    buf[3] = (uint8_t)cadExitMode;
    buf[4] = (uint8_t)((cadTimeout >> 16) & 0xFF);
    buf[5] = (uint8_t)((cadTimeout >> 8) & 0xFF);
    buf[6] = (uint8_t)(cadTimeout & 0xFF);
    SX1268_WriteCommand(RADIO_SET_CADPARAMS, buf, 7);

    radioOperatingMode = MODE_CAD;
}

void SX1268_SetBufferBaseAddress(uint8_t txBaseAddress, uint8_t rxBaseAddress) {
    uint8_t buf[2];
    buf[0] = txBaseAddress;
    buf[1] = rxBaseAddress;
    SX1268_WriteCommand(RADIO_SET_BUFFERBASEADDRESS, buf, 2);
}

radioStatus_t SX1268_GetStatus(void) {
    uint8_t stat = 0;
    radioStatus_t status;

    SX1268_ReadCommand(RADIO_GET_STATUS, (uint8_t *)&stat, 1);
    status.value = stat;

    return status;
}

int8_t SX1268_GetRssiInst(void) {
    uint8_t buf[1];
    int8_t rssi = 0;

    SX1268_ReadCommand(RADIO_GET_RSSIINST, buf, 1);
    rssi = -buf[0] >> 1;

    return rssi;
}

void SX1268_GetRxBufferStatus(uint8_t *payloadLength, uint8_t *rxStartBufferPointer) {
    uint8_t status[2];
    SX1268_ReadCommand(RADIO_GET_RXBUFFERSTATUS, status, 2);

    // In case of LORA fixed header, the payloadLength is obtained by reading
    // the register REG_LR_PAYLOADLENGTH
    if((SX1268_GetPacketType() == PACKET_TYPE_LORA) && (SX1268_ReadRegister(REG_LR_PACKETPARAMS) >> 7 == 1)) {
        *payloadLength = SX1268_ReadRegister(REG_LR_PAYLOADLENGTH);
    } else {
        *payloadLength = status[0];
    }

    *rxStartBufferPointer = status[1];
}

void SX1268_GetPacketStatus(packetStatus_t *pktStatus) {
    uint8_t status[3];
    SX1268_ReadCommand(RADIO_GET_PACKETSTATUS, status, 3);

    pktStatus->packetType = SX1268_GetPacketType();
    switch(pktStatus->packetType) {
        case PACKET_TYPE_GFSK:
            pktStatus->params.gfsk.rxStatus = status[0];
            pktStatus->params.gfsk.rssiSync = -status[1] >> 1;
            pktStatus->params.gfsk.rssiAvg = -status[2] >> 1;
            pktStatus->params.gfsk.freqError = 0;
            break;

        case PACKET_TYPE_LORA:
            pktStatus->params.loRa.rssiPkt = -status[0] >> 1;
            (status[1] < 128) ? (pktStatus->params.loRa.snrPkt = status[1] >> 2) : (pktStatus->params.loRa.snrPkt = ((status[1] - 256) >> 2));
            pktStatus->params.loRa.signalRssiPkt = -status[2] >> 1;
            pktStatus->params.loRa.freqError = frequencyError;
            break;

        default:
        case PACKET_TYPE_NONE:
            // In that specific case, we set everything in the pktStatus to zeros
            // and reset the packet type accordingly
            memset(pktStatus, 0, sizeof(packetStatus_t));
            pktStatus->packetType = PACKET_TYPE_NONE;
            break;
    }
}

radioError_t SX1268_GetDeviceErrors(void) {
    radioError_t error;
    SX1268_ReadCommand(RADIO_GET_ERROR, (uint8_t *)&error, 2);

    return error;
}

void SX1268_ClearDeviceErrors(void) {
    uint8_t buf[2] = {0x00, 0x00};
    SX1268_WriteCommand(RADIO_CLR_ERROR, buf, 2);
}

void SX1268_ClearIrqStatus(uint16_t irq) {
    uint8_t buf[2];
    buf[0] = (uint8_t)(((uint16_t)irq >> 8) & 0x00FF);
    buf[1] = (uint8_t)((uint16_t)irq & 0x00FF);
    SX1268_WriteCommand(RADIO_CLR_IRQSTATUS, buf, 2);
}
