/* ChuniF4 modifications (2026): IO4 descriptor and IN/OUT reports,
 * bounded calibration queue, EP0/interrupt normalisation and connection stats.
 * Original ST copyright and component SLA0044 license remain applicable.
 */
/**
  ******************************************************************************
  * @file    usbd_hid.h
  * @author  MCD Application Team
  * @brief   Header file for the usbd_hid_core.c file.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2015 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __USB_HID_H
#define __USB_HID_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include  "usbd_ioreq.h"

/** @addtogroup STM32_USB_DEVICE_LIBRARY
  * @{
  */

/** @defgroup USBD_HID
  * @brief This file is the Header file for usbd_hid.c
  * @{
  */


/** @defgroup USBD_HID_Exported_Defines
  * @{
  */
#ifndef HID_EPIN_ADDR
#define HID_EPIN_ADDR                              0x81U
#endif /* HID_EPIN_ADDR */
#define HID_EPIN_SIZE                              0x40U

#ifndef HID_EPOUT_ADDR
#define HID_EPOUT_ADDR                             0x01U
#endif /* HID_EPOUT_ADDR */
#define HID_EPOUT_SIZE                             0x40U

#define USB_HID_CONFIG_DESC_SIZ                    41U
#define USB_HID_DESC_SIZ                           9U
#define HID_MOUSE_REPORT_DESC_SIZE                 113U

#define HID_DESCRIPTOR_TYPE                        0x21U
#define HID_REPORT_DESC                            0x22U

#ifndef HID_HS_BINTERVAL
#define HID_HS_BINTERVAL                           0x07U
#endif /* HID_HS_BINTERVAL */

#ifndef HID_FS_BINTERVAL
#define HID_FS_BINTERVAL                           0x01U
#endif /* HID_FS_BINTERVAL */

#define USBD_HID_REQ_SET_PROTOCOL                       0x0BU
#define USBD_HID_REQ_GET_PROTOCOL                       0x03U

#define USBD_HID_REQ_SET_IDLE                           0x0AU
#define USBD_HID_REQ_GET_IDLE                           0x02U

#define USBD_HID_REQ_SET_REPORT                         0x09U
#define USBD_HID_REQ_GET_REPORT                         0x01U

/* IO4 Output Report layout, shared by the EP0 SET_REPORT path and the
   Interrupt OUT path. Normalised form is: Report ID + command + payload. */
#define IO4_OUT_REPORT_ID                               0x10U
#define IO4_OUT_REPORT_LEN                              64U
#define IO4_OUT_PAYLOAD_LEN                             62U
/**
  * @}
  */


/** @defgroup USBD_CORE_Exported_TypesDefinitions
  * @{
  */
typedef enum
{
  USBD_HID_IDLE = 0,
  USBD_HID_BUSY,
} USBD_HID_StateTypeDef;


typedef struct
{
  uint32_t Protocol;
  uint32_t IdleState;
  uint32_t AltSetting;
  USBD_HID_StateTypeDef state;

  uint8_t OutReport[64];
  uint8_t OutReportType;
  uint8_t OutReportId;
  uint16_t OutReportLength;

  uint8_t OutEpReport[HID_EPOUT_SIZE];
  uint16_t OutEpReportLength;
} USBD_HID_HandleTypeDef;

/* Snapshot of the IO4 Output Report parser counters. */
typedef struct
{
  uint8_t  last_output_cmd;
  uint16_t last_output_length;
  uint32_t output_packet_count;
  uint32_t malformed_output_count;
  uint32_t calibration_queue_drops;
  uint32_t connection_generation;
} USBD_HID_IO4StatsTypeDef;

/*
 * HID Class specification version 1.1
 * 6.2.1 HID Descriptor
 */

typedef struct
{
  uint8_t           bLength;
  uint8_t           bDescriptorType;
  uint16_t          bcdHID;
  uint8_t           bCountryCode;
  uint8_t           bNumDescriptors;
  uint8_t           bHIDDescriptorType;
  uint16_t          wItemLength;
} __PACKED USBD_HIDDescTypeDef;

/**
  * @}
  */



/** @defgroup USBD_CORE_Exported_Macros
  * @{
  */

/**
  * @}
  */

/** @defgroup USBD_CORE_Exported_Variables
  * @{
  */

extern USBD_ClassTypeDef USBD_HID;
#define USBD_HID_CLASS &USBD_HID
/**
  * @}
  */

/** @defgroup USB_CORE_Exported_Functions
  * @{
  */
#ifdef USE_USBD_COMPOSITE
uint8_t USBD_HID_SendReport(USBD_HandleTypeDef *pdev, uint8_t *report, uint16_t len, uint8_t ClassId);
#else
uint8_t USBD_HID_SendReport(USBD_HandleTypeDef *pdev, uint8_t *report, uint16_t len);
#endif /* USE_USBD_COMPOSITE */
uint32_t USBD_HID_GetPollingInterval(USBD_HandleTypeDef *pdev);
void USBD_HID_GetIO4Stats(USBD_HID_IO4StatsTypeDef *stats);
/* Main loop only. Copies the complete normalised 63-byte command. */
uint8_t USBD_HID_GetNextCalibrationCommand(uint8_t report[63]);
uint8_t USBD_HID_CanSendReport(USBD_HandleTypeDef *pdev);

/**
  * @}
  */

#ifdef __cplusplus
}
#endif

#endif  /* __USB_HID_H */
/**
  * @}
  */

/**
  * @}
  */

