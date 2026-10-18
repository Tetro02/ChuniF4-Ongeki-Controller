/* ChuniF4 modifications (2026): IO4 descriptor and IN/OUT reports,
 * bounded calibration queue, EP0/interrupt normalisation and connection stats.
 * Original ST copyright and component SLA0044 license remain applicable.
 */
/**
  ******************************************************************************
  * @file    usbd_hid.c
  * @author  MCD Application Team
  * @brief   This file provides the HID core functions.
  *
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
  * @verbatim
  *
  *          ===================================================================
  *                                HID Class  Description
  *          ===================================================================
  *           This module manages the HID class V1.11 following the "Device Class Definition
  *           for Human Interface Devices (HID) Version 1.11 Jun 27, 2001".
  *           This driver implements the following aspects of the specification:
  *             - The Boot Interface Subclass
  *             - The Mouse protocol
  *             - Usage Page : Generic Desktop
  *             - Usage : Joystick
  *             - Collection : Application
  *
  * @note     In HS mode and when the DMA is used, all variables and data structures
  *           dealing with the DMA during the transaction process should be 32-bit aligned.
  *
  *
  *  @endverbatim
  *
  ******************************************************************************
  */

/* BSPDependencies
- "stm32xxxxx_{eval}{discovery}{nucleo_144}.c"
- "stm32xxxxx_{eval}{discovery}_io.c"
EndBSPDependencies */

/* Includes ------------------------------------------------------------------*/
#include "usbd_hid.h"
#include "lever_calibration.h"
#include "usbd_ctlreq.h"


/** @addtogroup STM32_USB_DEVICE_LIBRARY
  * @{
  */


/** @defgroup USBD_HID
  * @brief usbd core module
  * @{
  */

/** @defgroup USBD_HID_Private_TypesDefinitions
  * @{
  */
/**
  * @}
  */


/** @defgroup USBD_HID_Private_Defines
  * @{
  */

/**
  * @}
  */


/** @defgroup USBD_HID_Private_Macros
  * @{
  */
/**
  * @}
  */


/** @defgroup USBD_HID_Private_FunctionPrototypes
  * @{
  */

static uint8_t USBD_HID_Init(USBD_HandleTypeDef *pdev, uint8_t cfgidx);
static uint8_t USBD_HID_DeInit(USBD_HandleTypeDef *pdev, uint8_t cfgidx);
static uint8_t USBD_HID_Setup(USBD_HandleTypeDef *pdev, USBD_SetupReqTypedef *req);
static uint8_t USBD_HID_EP0_RxReady(USBD_HandleTypeDef *pdev);
static uint8_t USBD_HID_DataIn(USBD_HandleTypeDef *pdev, uint8_t epnum);
static uint8_t USBD_HID_DataOut(USBD_HandleTypeDef *pdev, uint8_t epnum);
#ifndef USE_USBD_COMPOSITE
static uint8_t *USBD_HID_GetFSCfgDesc(uint16_t *length);
static uint8_t *USBD_HID_GetHSCfgDesc(uint16_t *length);
static uint8_t *USBD_HID_GetOtherSpeedCfgDesc(uint16_t *length);
static uint8_t *USBD_HID_GetDeviceQualifierDesc(uint16_t *length);
#endif /* USE_USBD_COMPOSITE  */
/**
  * @}
  */

/** @defgroup USBD_HID_Private_Variables
  * @{
  */

USBD_ClassTypeDef USBD_HID =
{
  USBD_HID_Init,
  USBD_HID_DeInit,
  USBD_HID_Setup,
  NULL,                     /* EP0_TxSent */
  USBD_HID_EP0_RxReady,     /* EP0_RxReady */
  USBD_HID_DataIn,          /* DataIn */
  USBD_HID_DataOut,         /* DataOut */
  NULL,                     /* SOF */
  NULL,
  NULL,
#ifdef USE_USBD_COMPOSITE
  NULL,
  NULL,
  NULL,
  NULL,
#else
  USBD_HID_GetHSCfgDesc,
  USBD_HID_GetFSCfgDesc,
  USBD_HID_GetOtherSpeedCfgDesc,
  USBD_HID_GetDeviceQualifierDesc,
#endif /* USE_USBD_COMPOSITE  */
};

#ifndef USE_USBD_COMPOSITE
/* USB HID device FS Configuration Descriptor */
__ALIGN_BEGIN static uint8_t USBD_HID_CfgDesc[USB_HID_CONFIG_DESC_SIZ] __ALIGN_END =
{
  0x09,                                               /* bLength: Configuration Descriptor size */
  USB_DESC_TYPE_CONFIGURATION,                        /* bDescriptorType: Configuration */
  USB_HID_CONFIG_DESC_SIZ,                            /* wTotalLength: Bytes returned */
  0x00,
  0x01,                                               /* bNumInterfaces: 1 interface */
  0x01,                                               /* bConfigurationValue: Configuration value */
  0x00,                                               /* iConfiguration: Index of string descriptor
                                                         describing the configuration */
#if (USBD_SELF_POWERED == 1U)
  0xE0,                                               /* bmAttributes: Bus Powered according to user configuration */
#else
  0xA0,                                               /* bmAttributes: Bus Powered according to user configuration */
#endif /* USBD_SELF_POWERED */
  USBD_MAX_POWER,                                     /* MaxPower (mA) */

  /************** Descriptor of Joystick Mouse interface ****************/
  /* 09 */
  0x09,                                               /* bLength: Interface Descriptor size */
  USB_DESC_TYPE_INTERFACE,                            /* bDescriptorType: Interface descriptor type */
  0x00,                                               /* bInterfaceNumber: Number of Interface */
  0x00,                                               /* bAlternateSetting: Alternate setting */
  0x02,                                               /* bNumEndpoints */
  0x03,                                               /* bInterfaceClass: HID */
  0x01,                                               /* bInterfaceSubClass : 1=BOOT, 0=no boot */
  0x02,                                               /* nInterfaceProtocol : 0=none, 1=keyboard, 2=mouse */
  0,                                                  /* iInterface: Index of string descriptor */
  /******************** Descriptor of Joystick Mouse HID ********************/
  /* 18 */
  0x09,                                               /* bLength: HID Descriptor size */
  HID_DESCRIPTOR_TYPE,                                /* bDescriptorType: HID */
  0x11,                                               /* bcdHID: HID Class Spec release number */
  0x01,
  0x00,                                               /* bCountryCode: Hardware target country */
  0x01,                                               /* bNumDescriptors: Number of HID class descriptors to follow */
  0x22,                                               /* bDescriptorType */
  HID_MOUSE_REPORT_DESC_SIZE,                         /* wItemLength: Total length of Report descriptor */
  0x00,
  /******************** Descriptor of Mouse endpoint ********************/
  /* 27 */
  0x07,                                               /* bLength: Endpoint Descriptor size */
  USB_DESC_TYPE_ENDPOINT,                             /* bDescriptorType:*/

  HID_EPIN_ADDR,                                      /* bEndpointAddress: Endpoint Address (IN) */
  0x03,                                               /* bmAttributes: Interrupt endpoint */
  HID_EPIN_SIZE,                                      /* wMaxPacketSize: 64 Bytes max */
  0x00,
  HID_FS_BINTERVAL,                                   /* bInterval: Polling Interval */
  /******************** Descriptor of the OUT endpoint ********************/
  /* 34 */
  0x07,                                               /* bLength: Endpoint Descriptor size */
  USB_DESC_TYPE_ENDPOINT,                             /* bDescriptorType:*/
  HID_EPOUT_ADDR,                                     /* bEndpointAddress: Endpoint Address (OUT) */
  0x03,                                               /* bmAttributes: Interrupt endpoint */
  HID_EPOUT_SIZE,                                     /* wMaxPacketSize: 64 Bytes max */
  0x00,
  HID_FS_BINTERVAL,                                   /* bInterval: Polling Interval */
  /* 41 */
};
#endif /* USE_USBD_COMPOSITE  */

/* USB HID device Configuration Descriptor */
__ALIGN_BEGIN static uint8_t USBD_HID_Desc[USB_HID_DESC_SIZ] __ALIGN_END =
{
  /* 18 */
  0x09,                                               /* bLength: HID Descriptor size */
  HID_DESCRIPTOR_TYPE,                                /* bDescriptorType: HID */
  0x11,                                               /* bcdHID: HID Class Spec release number */
  0x01,
  0x00,                                               /* bCountryCode: Hardware target country */
  0x01,                                               /* bNumDescriptors: Number of HID class descriptors to follow */
  0x22,                                               /* bDescriptorType */
  HID_MOUSE_REPORT_DESC_SIZE,                         /* wItemLength: Total length of Report descriptor */
  0x00,
};

#ifndef USE_USBD_COMPOSITE
/* USB Standard Device Descriptor */
__ALIGN_BEGIN static uint8_t USBD_HID_DeviceQualifierDesc[USB_LEN_DEV_QUALIFIER_DESC] __ALIGN_END =
{
  USB_LEN_DEV_QUALIFIER_DESC,
  USB_DESC_TYPE_DEVICE_QUALIFIER,
  0x00,
  0x02,
  0x00,
  0x00,
  0x00,
  0x40,
  0x01,
  0x00,
};
#endif /* USE_USBD_COMPOSITE  */

__ALIGN_BEGIN static uint8_t HID_MOUSE_ReportDesc[HID_MOUSE_REPORT_DESC_SIZE] __ALIGN_END =
{
    /* Joystick / IO4 style input */

    0x05, 0x01,                   // Usage Page (Generic Desktop)
    0x09, 0x04,                   // Usage (Joystick)
    0xA1, 0x01,                   // Collection (Application)

    0x85, 0x01,                   // Report ID 1

    0x09, 0x01,                   // Usage (Pointer)
    0xA1, 0x00,                   // Collection (Physical)

    /* 14 x 16-bit analog channels */
    0x09, 0x30,                   // X
    0x09, 0x31,                   // Y
    0x09, 0x30,                   // X
    0x09, 0x31,                   // Y
    0x09, 0x30,                   // X
    0x09, 0x31,                   // Y
    0x09, 0x30,                   // X
    0x09, 0x31,                   // Y
    0x09, 0x33,                   // Rx
    0x09, 0x34,                   // Ry
    0x09, 0x33,                   // Rx
    0x09, 0x34,                   // Ry
    0x09, 0x36,                   // Slider
    0x09, 0x36,                   // Slider

    0x15, 0x00,                   // Logical Minimum 0
    0x27, 0xFF, 0xFF, 0x00, 0x00,// Logical Maximum 65535
    0x35, 0x00,                   // Physical Minimum 0
    0x47, 0xFF, 0xFF, 0x00, 0x00,// Physical Maximum 65535

    0x95, 0x0E,                   // Report Count = 14
    0x75, 0x10,                   // Report Size = 16 bit
    0x81, 0x02,                   // Input (Data, Variable, Absolute)

    0xC0,                         // End Physical Collection


    /* 48 buttons */

    0x05, 0x02,                   // Usage Page (Simulation Controls)
    0x05, 0x09,                   // Usage Page (Button)

    0x19, 0x01,                   // Usage Minimum = Button 1
    0x29, 0x30,                   // Usage Maximum = Button 48

    0x15, 0x00,
    0x25, 0x01,
    0x45, 0x01,

    0x75, 0x01,                   // 1 bit
    0x95, 0x30,                   // 48 bits
    0x81, 0x02,                   // Input


    /* Padding to make input report 63-byte payload */

    0x09, 0x00,
    0x75, 0x08,
    0x95, 0x1D,                   // 29 bytes
    0x81, 0x01,                   // Constant


    /* Output Report ID 0x10 */

    0x06, 0xA0, 0xFF,             // Vendor Defined Usage Page
    0x09, 0x00,

    0x85, 0x10,                   // Report ID 16

    0xA1, 0x01,

    0x09, 0x00,
    0x15, 0x00,
    0x26, 0xFF, 0x00,

    0x75, 0x08,
    0x95, 0x3F,                   // 63 bytes output
    0x91, 0x02,                   // Output

    0xC0,
    0xC0                          // End Application Collection
};

static uint8_t HIDInEpAdd = HID_EPIN_ADDR;
static uint8_t HIDOutEpAdd = HID_EPOUT_ADDR;

/* IO4 Output Report parser state. Written from USB interrupt context
   (DataOut / EP0_RxReady) and read from the main loop, hence volatile. It is
   deliberately not cleared on re-enumeration so a host-side test can keep
   observing the counters across reconnects. */
static volatile uint8_t  IO4_LastOutputCmd = 0U;
static volatile uint16_t IO4_LastOutputLength = 0U;
static volatile uint32_t IO4_OutputPacketCount = 0U;
static volatile uint32_t IO4_MalformedOutputCount = 0U;
/* USB ISR copies before rearming OUT. Main copies with IRQs briefly masked.
   Five slots provide four pending commands. Only the LC extension is queued. */
#define IO4_CAL_QUEUE_SLOTS 5U
static uint8_t IO4_CalQueue[IO4_CAL_QUEUE_SLOTS][63];
static volatile uint8_t IO4_CalHead, IO4_CalTail;
static volatile uint32_t IO4_CalDrops, IO4_ConnectionGeneration;


/**
  * @}
  */

/** @defgroup USBD_HID_Private_Functions
  * @{
  */

/**
  * @brief  USBD_HID_Init
  *         Initialize the HID interface
  * @param  pdev: device instance
  * @param  cfgidx: Configuration index
  * @retval status
  */
static uint8_t USBD_HID_Init(USBD_HandleTypeDef *pdev, uint8_t cfgidx)
{
  UNUSED(cfgidx);
  IO4_CalHead = IO4_CalTail = 0U;
  ++IO4_ConnectionGeneration;

  USBD_HID_HandleTypeDef *hhid;

  hhid = (USBD_HID_HandleTypeDef *)USBD_malloc(sizeof(USBD_HID_HandleTypeDef));

  if (hhid == NULL)
  {
    pdev->pClassDataCmsit[pdev->classId] = NULL;
    return (uint8_t)USBD_EMEM;
  }

  pdev->pClassDataCmsit[pdev->classId] = (void *)hhid;
  pdev->pClassData = pdev->pClassDataCmsit[pdev->classId];

#ifdef USE_USBD_COMPOSITE
  /* Get the Endpoints addresses allocated for this class instance */
  HIDInEpAdd  = USBD_CoreGetEPAdd(pdev, USBD_EP_IN, USBD_EP_TYPE_INTR, (uint8_t)pdev->classId);
#endif /* USE_USBD_COMPOSITE */

  if (pdev->dev_speed == USBD_SPEED_HIGH)
  {
    pdev->ep_in[HIDInEpAdd & 0xFU].bInterval = HID_HS_BINTERVAL;
    pdev->ep_out[HIDOutEpAdd & 0xFU].bInterval = HID_HS_BINTERVAL;
  }
  else   /* LOW and FULL-speed endpoints */
  {
    pdev->ep_in[HIDInEpAdd & 0xFU].bInterval = HID_FS_BINTERVAL;
    pdev->ep_out[HIDOutEpAdd & 0xFU].bInterval = HID_FS_BINTERVAL;
  }

  /* Open EP IN */
  (void)USBD_LL_OpenEP(pdev, HIDInEpAdd, USBD_EP_TYPE_INTR, HID_EPIN_SIZE);
  pdev->ep_in[HIDInEpAdd & 0xFU].is_used = 1U;

  /* Open EP OUT and arm it, so the first Output Report the host writes is
     accepted instead of being NAKed until someone arms it later. */
  (void)USBD_LL_OpenEP(pdev, HIDOutEpAdd, USBD_EP_TYPE_INTR, HID_EPOUT_SIZE);
  pdev->ep_out[HIDOutEpAdd & 0xFU].is_used = 1U;

  hhid->OutEpReportLength = 0U;
  (void)USBD_LL_PrepareReceive(pdev, HIDOutEpAdd, hhid->OutEpReport, HID_EPOUT_SIZE);

  hhid->state = USBD_HID_IDLE;

  return (uint8_t)USBD_OK;
}

/**
  * @brief  USBD_HID_DeInit
  *         DeInitialize the HID layer
  * @param  pdev: device instance
  * @param  cfgidx: Configuration index
  * @retval status
  */
static uint8_t USBD_HID_DeInit(USBD_HandleTypeDef *pdev, uint8_t cfgidx)
{
  UNUSED(cfgidx);
  IO4_CalHead = IO4_CalTail = 0U;
  ++IO4_ConnectionGeneration;

#ifdef USE_USBD_COMPOSITE
  /* Get the Endpoints addresses allocated for this class instance */
  HIDInEpAdd  = USBD_CoreGetEPAdd(pdev, USBD_EP_IN, USBD_EP_TYPE_INTR, (uint8_t)pdev->classId);
#endif /* USE_USBD_COMPOSITE */

  /* Close HID EPs */
  (void)USBD_LL_CloseEP(pdev, HIDInEpAdd);
  pdev->ep_in[HIDInEpAdd & 0xFU].is_used = 0U;
  pdev->ep_in[HIDInEpAdd & 0xFU].bInterval = 0U;

  (void)USBD_LL_CloseEP(pdev, HIDOutEpAdd);
  pdev->ep_out[HIDOutEpAdd & 0xFU].is_used = 0U;
  pdev->ep_out[HIDOutEpAdd & 0xFU].bInterval = 0U;

  /* Free allocated memory */
  if (pdev->pClassDataCmsit[pdev->classId] != NULL)
  {
    (void)USBD_free(pdev->pClassDataCmsit[pdev->classId]);
    pdev->pClassDataCmsit[pdev->classId] = NULL;
  }

  return (uint8_t)USBD_OK;
}

/**
  * @brief  USBD_HID_Setup
  *         Handle the HID specific requests
  * @param  pdev: instance
  * @param  req: usb requests
  * @retval status
  */
static uint8_t USBD_HID_Setup(USBD_HandleTypeDef *pdev, USBD_SetupReqTypedef *req)
{
  USBD_HID_HandleTypeDef *hhid = (USBD_HID_HandleTypeDef *)pdev->pClassDataCmsit[pdev->classId];
  USBD_StatusTypeDef ret = USBD_OK;
  uint16_t len;
  uint8_t *pbuf;
  uint16_t status_info = 0U;

  if (hhid == NULL)
  {
    return (uint8_t)USBD_FAIL;
  }

  switch (req->bmRequest & USB_REQ_TYPE_MASK)
  {
    case USB_REQ_TYPE_CLASS :
      switch (req->bRequest)
      {
        case USBD_HID_REQ_SET_PROTOCOL:
          hhid->Protocol = (uint8_t)(req->wValue);
          break;

        case USBD_HID_REQ_GET_PROTOCOL:
          (void)USBD_CtlSendData(pdev, (uint8_t *)&hhid->Protocol, 1U);
          break;

        case USBD_HID_REQ_SET_IDLE:
          hhid->IdleState = (uint8_t)(req->wValue >> 8);
          break;

        case USBD_HID_REQ_GET_IDLE:
          (void)USBD_CtlSendData(pdev, (uint8_t *)&hhid->IdleState, 1U);
          break;

        case USBD_HID_REQ_SET_REPORT:
        {
          uint8_t report_type = (uint8_t)((req->wValue >> 8) & 0xFFU);
          uint8_t report_id   = (uint8_t)(req->wValue & 0xFFU);

          /* HID Report Type: 1 = Input, 2 = Output, 3 = Feature.
             Only Output Reports are accepted; the payload is received on EP0
             into OutReport[] and handed to USBD_HID_EP0_RxReady() once it has
             fully arrived. */
          if ((report_type != 0x02U) ||
              (req->wLength == 0U) ||
              (req->wLength > sizeof(hhid->OutReport)))
          {
            USBD_CtlError(pdev, req);
            ret = USBD_FAIL;
            break;
          }

          hhid->OutReportType   = report_type;
          hhid->OutReportId     = report_id;
          hhid->OutReportLength = req->wLength;

          /* No short transfer may reuse bytes from a previous command. */
          memset(hhid->OutReport, 0, sizeof(hhid->OutReport));

          (void)USBD_CtlPrepareRx(pdev, hhid->OutReport, req->wLength);

          break;
        }

        default:
          USBD_CtlError(pdev, req);
          ret = USBD_FAIL;
          break;
      }
      break;
    case USB_REQ_TYPE_STANDARD:
      switch (req->bRequest)
      {
        case USB_REQ_GET_STATUS:
          if (pdev->dev_state == USBD_STATE_CONFIGURED)
          {
            (void)USBD_CtlSendData(pdev, (uint8_t *)&status_info, 2U);
          }
          else
          {
            USBD_CtlError(pdev, req);
            ret = USBD_FAIL;
          }
          break;

        case USB_REQ_GET_DESCRIPTOR:
          if ((req->wValue >> 8) == HID_REPORT_DESC)
          {
            len = MIN(HID_MOUSE_REPORT_DESC_SIZE, req->wLength);
            pbuf = HID_MOUSE_ReportDesc;
          }
          else if ((req->wValue >> 8) == HID_DESCRIPTOR_TYPE)
          {
            pbuf = USBD_HID_Desc;
            len = MIN(USB_HID_DESC_SIZ, req->wLength);
          }
          else
          {
            USBD_CtlError(pdev, req);
            ret = USBD_FAIL;
            break;
          }
          (void)USBD_CtlSendData(pdev, pbuf, len);
          break;

        case USB_REQ_GET_INTERFACE :
          if (pdev->dev_state == USBD_STATE_CONFIGURED)
          {
            (void)USBD_CtlSendData(pdev, (uint8_t *)&hhid->AltSetting, 1U);
          }
          else
          {
            USBD_CtlError(pdev, req);
            ret = USBD_FAIL;
          }
          break;

        case USB_REQ_SET_INTERFACE:
          if (pdev->dev_state == USBD_STATE_CONFIGURED)
          {
            hhid->AltSetting = (uint8_t)(req->wValue);
          }
          else
          {
            USBD_CtlError(pdev, req);
            ret = USBD_FAIL;
          }
          break;

        case USB_REQ_CLEAR_FEATURE:
          break;

        default:
          USBD_CtlError(pdev, req);
          ret = USBD_FAIL;
          break;
      }
      break;

    default:
      USBD_CtlError(pdev, req);
      ret = USBD_FAIL;
      break;
  }

  return (uint8_t)ret;
}


/**
  * @brief  USBD_HID_GetIO4Stats
  *         Copy the IO4 Output Report parser counters out into a snapshot
  * @param  stats: destination, must not be NULL
  * @retval None
  */
void USBD_HID_GetIO4Stats(USBD_HID_IO4StatsTypeDef *stats)
{
  if (stats == NULL)
  {
    return;
  }

  stats->last_output_cmd        = IO4_LastOutputCmd;
  stats->last_output_length     = IO4_LastOutputLength;
  stats->output_packet_count    = IO4_OutputPacketCount;
  stats->malformed_output_count = IO4_MalformedOutputCount;
  stats->calibration_queue_drops = IO4_CalDrops;
  stats->connection_generation = IO4_ConnectionGeneration;
}

uint8_t USBD_HID_GetNextCalibrationCommand(uint8_t report[63])
{
  uint32_t primask;
  uint8_t found = 0U;
  if (report == NULL) { return 0U; }
  primask = __get_PRIMASK();
  __disable_irq();
  if (IO4_CalTail != IO4_CalHead)
  {
    memcpy(report, IO4_CalQueue[IO4_CalTail], 63U);
    IO4_CalTail = (uint8_t)((IO4_CalTail + 1U) % IO4_CAL_QUEUE_SLOTS);
    found = 1U;
  }
  __set_PRIMASK(primask);
  return found;
}

uint8_t USBD_HID_CanSendReport(USBD_HandleTypeDef *pdev)
{
  USBD_HID_HandleTypeDef *hhid = (USBD_HID_HandleTypeDef *)pdev->pClassDataCmsit[pdev->classId];
  return (pdev->dev_state == USBD_STATE_CONFIGURED && hhid != NULL &&
          hhid->state == USBD_HID_IDLE) ? 1U : 0U;
}

/**
  * @brief  USBD_HID_SendReport
  *         Send HID Report
  * @param  pdev: device instance
  * @param  buff: pointer to report
  * @param  ClassId: The Class ID
  * @retval status
  */
#ifdef USE_USBD_COMPOSITE
uint8_t USBD_HID_SendReport(USBD_HandleTypeDef *pdev, uint8_t *report, uint16_t len, uint8_t ClassId)
{
  USBD_HID_HandleTypeDef *hhid = (USBD_HID_HandleTypeDef *)pdev->pClassDataCmsit[ClassId];
#else
uint8_t USBD_HID_SendReport(USBD_HandleTypeDef *pdev, uint8_t *report, uint16_t len)
{
  USBD_HID_HandleTypeDef *hhid = (USBD_HID_HandleTypeDef *)pdev->pClassDataCmsit[pdev->classId];
#endif /* USE_USBD_COMPOSITE */

  if (hhid == NULL)
  {
    return (uint8_t)USBD_FAIL;
  }

#ifdef USE_USBD_COMPOSITE
  /* Get the Endpoints addresses allocated for this class instance */
  HIDInEpAdd = USBD_CoreGetEPAdd(pdev, USBD_EP_IN, USBD_EP_TYPE_INTR, ClassId);
#endif /* USE_USBD_COMPOSITE */

  if (pdev->dev_state == USBD_STATE_CONFIGURED)
  {
    if (hhid->state == USBD_HID_IDLE)
    {
      hhid->state = USBD_HID_BUSY;
      (void)USBD_LL_Transmit(pdev, HIDInEpAdd, report, len);
    }
  }

  return (uint8_t)USBD_OK;
}

/**
  * @brief  USBD_HID_GetPollingInterval
  *         return polling interval from endpoint descriptor
  * @param  pdev: device instance
  * @retval polling interval
  */
uint32_t USBD_HID_GetPollingInterval(USBD_HandleTypeDef *pdev)
{
  uint32_t polling_interval;

  /* HIGH-speed endpoints */
  if (pdev->dev_speed == USBD_SPEED_HIGH)
  {
    /* Sets the data transfer polling interval for high speed transfers.
     Values between 1..16 are allowed. Values correspond to interval
     of 2 ^ (bInterval-1). This option (8 ms, corresponds to HID_HS_BINTERVAL */
    polling_interval = (((1U << (HID_HS_BINTERVAL - 1U))) / 8U);
  }
  else   /* LOW and FULL-speed endpoints */
  {
    /* Sets the data transfer polling interval for low and full
    speed transfers */
    polling_interval =  HID_FS_BINTERVAL;
  }

  return ((uint32_t)(polling_interval));
}

#ifndef USE_USBD_COMPOSITE
/**
  * @brief  USBD_HID_GetCfgFSDesc
  *         return FS configuration descriptor
  * @param  speed : current device speed
  * @param  length : pointer data length
  * @retval pointer to descriptor buffer
  */
static uint8_t *USBD_HID_GetFSCfgDesc(uint16_t *length)
{
  USBD_EpDescTypeDef *pEpDesc = USBD_GetEpDesc(USBD_HID_CfgDesc, HID_EPIN_ADDR);

  if (pEpDesc != NULL)
  {
    pEpDesc->bInterval = HID_FS_BINTERVAL;
  }

  *length = (uint16_t)sizeof(USBD_HID_CfgDesc);
  return USBD_HID_CfgDesc;
}

/**
  * @brief  USBD_HID_GetCfgHSDesc
  *         return HS configuration descriptor
  * @param  speed : current device speed
  * @param  length : pointer data length
  * @retval pointer to descriptor buffer
  */
static uint8_t *USBD_HID_GetHSCfgDesc(uint16_t *length)
{
  USBD_EpDescTypeDef *pEpDesc = USBD_GetEpDesc(USBD_HID_CfgDesc, HID_EPIN_ADDR);

  if (pEpDesc != NULL)
  {
    pEpDesc->bInterval = HID_HS_BINTERVAL;
  }

  *length = (uint16_t)sizeof(USBD_HID_CfgDesc);
  return USBD_HID_CfgDesc;
}

/**
  * @brief  USBD_HID_GetOtherSpeedCfgDesc
  *         return other speed configuration descriptor
  * @param  speed : current device speed
  * @param  length : pointer data length
  * @retval pointer to descriptor buffer
  */
static uint8_t *USBD_HID_GetOtherSpeedCfgDesc(uint16_t *length)
{
  USBD_EpDescTypeDef *pEpDesc = USBD_GetEpDesc(USBD_HID_CfgDesc, HID_EPIN_ADDR);

  if (pEpDesc != NULL)
  {
    pEpDesc->bInterval = HID_FS_BINTERVAL;
  }

  *length = (uint16_t)sizeof(USBD_HID_CfgDesc);
  return USBD_HID_CfgDesc;
}
#endif /* USE_USBD_COMPOSITE  */

/**
  * @brief  IO4_HandleOutputReport
  *         Normalise an IO4 Output Report arriving on either receive path and
  *         record it. Called from USB interrupt context: it must not block,
  *         must not touch Flash, and must not store the data pointer - that
  *         points into OutEpReport[], which is re-armed and then overwritten by
  *         the next packet.
  * @param  data: received buffer
  * @param  len: number of bytes actually received
  * @param  external_report_id: Report ID carried in wValue (EP0 path only)
  * @param  report_id_is_external: 1 when the Report ID must come from
  *         external_report_id, 0 when it is the first byte of data
  * @retval None
  */
static void IO4_HandleOutputReport(const uint8_t *data,
                                   uint16_t len,
                                   uint8_t external_report_id,
                                   uint8_t report_id_is_external)
{
  const uint8_t *report;   /* command byte, Report ID already consumed */
  uint16_t report_len;     /* number of bytes left after the Report ID */

  if (data == NULL)
  {
    IO4_MalformedOutputCount++;
    return;
  }

  if (report_id_is_external == 0U)
  {
    /* Interrupt OUT path: the Report ID is the first byte of the buffer. */
    if ((len != IO4_OUT_REPORT_LEN) || (data[0] != IO4_OUT_REPORT_ID))
    {
      IO4_MalformedOutputCount++;
      return;
    }

    report     = &data[1];
    report_len = (uint16_t)(len - 1U);
  }
  else
  {
    /* EP0 SET_REPORT path: the Report ID arrives in wValue. Windows hosts
       normally repeat it as the first payload byte, but the HID spec does not
       require it, so accept both forms and reject everything else rather than
       guessing. */
    if (external_report_id != IO4_OUT_REPORT_ID)
    {
      IO4_MalformedOutputCount++;
      return;
    }

    if ((len == IO4_OUT_REPORT_LEN) && (data[0] == IO4_OUT_REPORT_ID))
    {
      report     = &data[1];          /* Report ID also present in the buffer */
      report_len = (uint16_t)(len - 1U);
    }
    else if (len == (IO4_OUT_REPORT_LEN - 1U))
    {
      report     = data;              /* Report ID only in wValue */
      report_len = len;
    }
    else
    {
      IO4_MalformedOutputCount++;
      return;
    }
  }

  /* Every accepted form must normalise to exactly one command byte plus the
     IO4 payload. */
  if (report_len != (IO4_OUT_PAYLOAD_LEN + 1U))
  {
    IO4_MalformedOutputCount++;
    return;
  }

  /* Queue an owned copy; calibration never runs in interrupt context. */
  if (report[0] == LEVER_CAL_COMMAND && report[1] == 'L' && report[2] == 'C')
  {
    uint8_t next = (uint8_t)((IO4_CalHead + 1U) % IO4_CAL_QUEUE_SLOTS);
    if (next == IO4_CalTail) { ++IO4_CalDrops; }
    else
    {
      memcpy(IO4_CalQueue[IO4_CalHead], report, 63U);
      __DMB();
      IO4_CalHead = next;
    }
  }
  IO4_LastOutputCmd    = report[0];
  IO4_LastOutputLength = len;
  IO4_OutputPacketCount++;
}

/**
  * @brief  USBD_HID_EP0_RxReady
  *         handle the data OUT stage of a control transfer on EP0, that is an
  *         Output Report delivered through a HID SET_REPORT request
  * @param  pdev: device instance
  * @retval status
  */
static uint8_t USBD_HID_EP0_RxReady(USBD_HandleTypeDef *pdev)
{
  USBD_HID_HandleTypeDef *hhid = (USBD_HID_HandleTypeDef *)pdev->pClassDataCmsit[pdev->classId];

  if (hhid == NULL)
  {
    return (uint8_t)USBD_FAIL;
  }

  /* Only Output Reports reach the parser; it validates the Report ID itself
     and counts anything it cannot normalise as malformed.

     The status stage is sent by the core immediately after this callback
     returns (usbd_core.c, USBD_LL_DataOutStage), so USBD_CtlSendStatus() must
     NOT be called from here. */
  if (hhid->OutReportType == 0x02U)
  {
    /* Reports fit in one EP0 packet. Validate actual receipt against wLength
       before normalising; wLength alone is not proof of received bytes. */
    uint16_t actual = (uint16_t)USBD_LL_GetRxDataSize(pdev, 0U);
    if (actual != hhid->OutReportLength) { ++IO4_MalformedOutputCount; }
    else
    {
      IO4_HandleOutputReport(hhid->OutReport, actual, hhid->OutReportId, 1U);
    }
    hhid->OutReportType = 0U;
  }

  return (uint8_t)USBD_OK;
}

/**
  * @brief  USBD_HID_DataOut
  *         handle data OUT Stage on the Interrupt OUT endpoint, that is an
  *         Output Report written straight to EP 0x01 instead of going through
  *         a HID SET_REPORT control transfer on EP0
  * @param  pdev: device instance
  * @param  epnum: endpoint index
  * @retval status
  */
static uint8_t USBD_HID_DataOut(USBD_HandleTypeDef *pdev, uint8_t epnum)
{
  USBD_HID_HandleTypeDef *hhid = (USBD_HID_HandleTypeDef *)pdev->pClassDataCmsit[pdev->classId];

  if (hhid == NULL)
  {
    return (uint8_t)USBD_FAIL;
  }

  /* The report has already landed in hhid->OutEpReport by the time this runs. */
  hhid->OutEpReportLength = (uint16_t)USBD_LL_GetRxDataSize(pdev, epnum);

  /* Parse BEFORE re-arming, and synchronously. Once USBD_LL_PrepareReceive()
     runs again the endpoint may overwrite OutEpReport with the next packet, so
     the buffer must not outlive this call and the parser must not block. */
  IO4_HandleOutputReport(hhid->OutEpReport,
                         hhid->OutEpReportLength,
                         0U,   /* Report ID is not external on this path ... */
                         0U);  /* ... it is the first byte of the buffer    */

  /* Re-arm: an Interrupt OUT endpoint delivers a single report per transfer,
     so it must be armed again for the next one. */
  (void)USBD_LL_PrepareReceive(pdev, HIDOutEpAdd, hhid->OutEpReport, HID_EPOUT_SIZE);

  return (uint8_t)USBD_OK;
}

/**
  * @brief  USBD_HID_DataIn
  *         handle data IN Stage
  * @param  pdev: device instance
  * @param  epnum: endpoint index
  * @retval status
  */
static uint8_t USBD_HID_DataIn(USBD_HandleTypeDef *pdev, uint8_t epnum)
{
  UNUSED(epnum);
  /* Ensure that the FIFO is empty before a new transfer, this condition could
  be caused by  a new transfer before the end of the previous transfer */
  ((USBD_HID_HandleTypeDef *)pdev->pClassDataCmsit[pdev->classId])->state = USBD_HID_IDLE;

  return (uint8_t)USBD_OK;
}

#ifndef USE_USBD_COMPOSITE
/**
  * @brief  DeviceQualifierDescriptor
  *         return Device Qualifier descriptor
  * @param  length : pointer data length
  * @retval pointer to descriptor buffer
  */
static uint8_t *USBD_HID_GetDeviceQualifierDesc(uint16_t *length)
{
  *length = (uint16_t)sizeof(USBD_HID_DeviceQualifierDesc);

  return USBD_HID_DeviceQualifierDesc;
}
#endif /* USE_USBD_COMPOSITE  */
/**
  * @}
  */


/**
  * @}
  */


/**
  * @}
  */

