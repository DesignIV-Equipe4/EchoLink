/*
 * echolink_can_protocol.h
 *
 *  Created on: 26 nov. 2026
 *      Author: gagno
 */

#ifndef INC_PROTOCOLS_ECHOLINK_CAN_PROTOCOL_H_
#define INC_PROTOCOLS_ECHOLINK_CAN_PROTOCOL_H_


#include <stdint.h>

/* Protocol versioning */
#define CAN_PROTOCOL_VERSION_MAJOR 1
#define CAN_PROTOCOL_VERSION_MINOR 0
#define CAN_PROTOCOL_VERSIOM_PATCH 0

/* Flag masks */
// ...

/* Enums */
// ...

/* Data structures */
/*
typedef struct __attribute__((packed)) {
	uint8_t  	version_major;
	uint8_t  	version_minor;
	uint8_t		version_patch;
	uint16_t 	payload_size;
    uint32_t    time_boot_ms;

    // ...
} echolink_can_data_t;
#define CAN_PROTOCOL_SIZE (sizeof(echolink_can_data_t))
*/


#endif /* INC_PROTOCOLS_ECHOLINK_CAN_PROTOCOL_H_ */
