/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "usb_device.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "usbd_hid.h"
#include "lever_calibration.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/*
 * IO4 Input Report
 *
 * Byte 0      :	  Report ID = 0x01
 * Byte 1~16   : ADC[0..7], uint16 little-endian
 * Byte 17~24  : Spinner[0..3]
 * Byte 25~28  : Chute[0..1]
 * Byte 29~30  : Buttons[0]
 * Byte 31~32  : Buttons[1]
 * Byte 33     : System Status
 * Byte 34     : USB Status
 * Byte 35~63  : LC v1 calibration status extension
 */
#define IO4_REPORT_SIZE        64U
#define IO4_REPORT_ID          0x01U

/*
 * ADC -> IO4 Analog 的映射与三点校准全部由 lever_calibration 模块负责。
 * 本文件不再持有任何转换参数。
 */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
ADC_HandleTypeDef hadc1;
DMA_HandleTypeDef hdma_adc1;

/* USER CODE BEGIN PV */

extern USBD_HandleTypeDef hUsbDeviceFS;

/*
 * DMA不断把ADC最新结果写入这里
 */
volatile uint16_t adc_raw = 2048U;

/*
 * 1ms HID发送节拍
 */
static uint32_t last_hid_tick = 0U;

/*
 * 调试状态刷新节拍（100ms）
 */
static uint32_t last_debug_tick = 0U;

/*
 * 硬件验收调试变量。
 *
 * 只供调试器在线观察，不参与任何逻辑判断。
 * io4_debug_stats 每 100ms 非阻塞刷新一次。
 */
volatile USBD_HID_IO4StatsTypeDef io4_debug_stats;
volatile uint16_t io4_debug_lever = 0U;
volatile uint16_t io4_debug_buttons0 = 0U;
volatile uint16_t io4_debug_buttons1 = 0U;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);
static void MX_ADC1_Init(void);

/* USER CODE BEGIN PFP */

static void IO4_WriteU16(uint8_t *buffer, uint16_t value);
static void IO4_ReadButtons(uint16_t *buttons0, uint16_t *buttons1);
static void IO4_SendInputReport(void);
static void IO4_Task(void);

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */


/*
 * 将uint16_t按照Little Endian写入USB Report。
 *
 * IO4的数据是Little Endian。
 */
static void IO4_WriteU16(uint8_t *buffer, uint16_t value)
{
    buffer[0] = (uint8_t)(value & 0xFFU);
    buffer[1] = (uint8_t)((value >> 8) & 0xFFU);
}


/*
 * 读取音击10个数字输入。
 *
 * 当前GPIO全部是：
 *
 * GPIO_PULLUP
 *
 * 因此：
 *
 * 普通八键：HIGH = 松开，LOW = 按下
 * 两个侧键 LS / RS：HIGH = 按下，LOW = 松开
 *
 *
 * 映射参考 ongeeki-firmware：
 *
 * 左侧：
 * A -> LA
 * S -> LB
 * D -> LC
 * MOUSE_L -> LS
 * U -> LM
 *
 * 右侧：
 * J -> RA
 * K -> RB
 * L -> RC
 * MOUSE_R -> RS
 * O -> RM
 */
static void IO4_ReadButtons(uint16_t *buttons0, uint16_t *buttons1)
{
    uint16_t b0 = 0U;
    uint16_t b1 = 0U;


    /* =====================================================
       Left A
       player 0 / bit 0
       ===================================================== */
    if (HAL_GPIO_ReadPin(KEY_A_GPIO_Port,
                         KEY_A_Pin) == GPIO_PIN_RESET)
    {
        b0 |= (1U << 0);
    }


    /* =====================================================
       Left B
       player 0 / bit 5
       ===================================================== */
    if (HAL_GPIO_ReadPin(KEY_S_GPIO_Port,
                         KEY_S_Pin) == GPIO_PIN_RESET)
    {
        b0 |= (1U << 5);
    }


    /* =====================================================
       Left C
       player 0 / bit 4
       ===================================================== */
    if (HAL_GPIO_ReadPin(KEY_D_GPIO_Port,
                         KEY_D_Pin) == GPIO_PIN_RESET)
    {
        b0 |= (1U << 4);
    }


    /* =====================================================
       Left Side
       player 1 / bit 15
       ===================================================== */
    if (HAL_GPIO_ReadPin(MOUSE_L_GPIO_Port,
                         MOUSE_L_Pin) == GPIO_PIN_SET)
    {
        b1 |= (1U << 15);
    }


    /* =====================================================
       Left Menu
       player 1 / bit 14
       ===================================================== */
    if (HAL_GPIO_ReadPin(KEY_U_GPIO_Port,
                         KEY_U_Pin) == GPIO_PIN_RESET)
    {
        b1 |= (1U << 14);
    }


    /* =====================================================
       Right A
       player 0 / bit 1
       ===================================================== */
    if (HAL_GPIO_ReadPin(KEY_J_GPIO_Port,
                         KEY_J_Pin) == GPIO_PIN_RESET)
    {
        b0 |= (1U << 1);
    }


    /* =====================================================
       Right B
       player 1 / bit 0
       ===================================================== */
    if (HAL_GPIO_ReadPin(KEY_K_GPIO_Port,
                         KEY_K_Pin) == GPIO_PIN_RESET)
    {
        b1 |= (1U << 0);
    }


    /* =====================================================
       Right C
       player 0 / bit 15
       ===================================================== */
    if (HAL_GPIO_ReadPin(KEY_L_GPIO_Port,
                         KEY_L_Pin) == GPIO_PIN_RESET)
    {
        b0 |= (1U << 15);
    }


    /* =====================================================
       Right Side
       player 0 / bit 14
       ===================================================== */
    if (HAL_GPIO_ReadPin(MOUSE_R_GPIO_Port,
                         MOUSE_R_Pin) == GPIO_PIN_SET)
    {
        b0 |= (1U << 14);
    }


    /* =====================================================
       Right Menu
       player 0 / bit 13
       ===================================================== */
    if (HAL_GPIO_ReadPin(KEY_O_GPIO_Port,
                         KEY_O_Pin) == GPIO_PIN_RESET)
    {
        b0 |= (1U << 13);
    }


    *buttons0 = b0;
    *buttons1 = b1;
}


/*
 * 构造并发送完整64-byte IO4 Input Report
 */
static void IO4_SendInputReport(void)
{
    /* USB transmits asynchronously: storage must outlive this function.
       IO4_Task only fills it when the previous transfer has completed. */
    static uint8_t report[IO4_REPORT_SIZE];

    uint16_t buttons0;
    uint16_t buttons1;

    uint16_t lever;


    /*
     * Report ID
     */
    report[0] = IO4_REPORT_ID;


    /*
     * Lever
     *
     * IO4:
     * ADC0 = Byte 1~2
     *
     * ADC -> IO4 的映射由 lever_calibration 模块负责。
     */
    lever = LeverCal_Convert(adc_raw);

    io4_debug_lever = lever;

    IO4_WriteU16(&report[1], lever);


    /*
     * ADC1~ADC7
     *
     * 当前全部保持0。
     *
     * report[3] ~ report[16]
     */


    /*
     * Spinner 0~3
     *
     * 当前全部为0。
     *
     * report[17] ~ report[24]
     */


    /*
     * Chute / Coin inputs
     *
     * 当前全部为0。
     *
     * report[25] ~ report[28]
     */


    /*
     * Buttons
     */
    IO4_ReadButtons(&buttons0, &buttons1);

    io4_debug_buttons0 = buttons0;
    io4_debug_buttons1 = buttons1;

    /*
     * Buttons[0]
     * Byte 29~30
     */
    IO4_WriteU16(&report[29], buttons0);

    /*
     * Buttons[1]
     * Byte 31~32
     */
    IO4_WriteU16(&report[31], buttons1);


    /*
     * System Status
     *
     * 完整 IO4 系统控制尚未实现，此字段保持 0。
     * 校准命令通过 0x10/C0/LC 扩展处理，状态写在 byte35..63。
     */
    report[33] = 0x00;


    /*
     * USB Status
     */
    report[34] = 0x00;


    /*
     * Byte 35~63: versioned lever calibration telemetry
     */


    LeverCal_WriteStatus(&report[35]);
    {
        USBD_HID_IO4StatsTypeDef stats;
        USBD_HID_GetIO4Stats(&stats);
        IO4_WriteU16(&report[62], (uint16_t)stats.calibration_queue_drops);
    }

    USBD_HID_SendReport(&hUsbDeviceFS,
                        report,
                        IO4_REPORT_SIZE);
}


/*
 * USB HID 1kHz发送任务
 */
static void IO4_Task(void)
{
    uint32_t now;
    static uint32_t generation;
    static uint32_t save_notice_started;
    static uint8_t save_notice_sent;
    uint8_t command[LEVER_CAL_COMMAND_SIZE];
    USBD_HID_IO4StatsTypeDef stats;

    now = HAL_GetTick();


    /*
     * 每1ms最多发送一帧
     */
    if (now == last_hid_tick)
    {
        return;
    }

    last_hid_tick = now;
    USBD_HID_GetIO4Stats(&stats);
    if (generation != stats.connection_generation)
    {
        generation = stats.connection_generation;
        LeverCal_Cancel(); /* never carry a pending capture across USB reset */
        save_notice_sent = 0U;
    }


    /*
     * USB没有完成枚举时不发送
     */
    if (hUsbDeviceFS.dev_state != USBD_STATE_CONFIGURED)
    {
        LeverCal_Cancel();
        save_notice_sent = 0U;
        return;
    }
    if (USBD_HID_GetNextCalibrationCommand(command))
    {
        (void)LeverCal_HandleCommand(command, sizeof(command), now);
    }
    LeverCal_Task(adc_raw, now,
        HAL_ADC_GetError(&hadc1) == HAL_ADC_ERROR_NONE &&
        HAL_DMA_GetError(&hdma_adc1) == HAL_DMA_ERROR_NONE &&
        (ADC1->CR2 & ADC_CR2_ADON) != 0U &&
        (hdma_adc1.Instance->CR & DMA_SxCR_EN) != 0U);
    if (LeverCal_SavePending())
    {
        if (save_notice_sent == 0U)
        {
            save_notice_started = now;
            save_notice_sent = 1U;
        }
        if (save_notice_sent == 1U)
        {
            /* Make the pending state visible before any potentially long erase. */
            if (USBD_HID_CanSendReport(&hUsbDeviceFS))
            {
                IO4_SendInputReport();
                save_notice_sent = 2U;
            }
            else if ((uint32_t)(now - save_notice_started) > 3000U)
            {
                LeverCal_SaveNoticeTimedOut();
                save_notice_sent = 0U;
            }
            return;
        }
        if (USBD_HID_CanSendReport(&hUsbDeviceFS))
        {
            /* The SAVING report reached the host. HAL keeps IRQs enabled;
               single-bank Flash busy can still pause code fetch / USB service. */
            LeverCal_ProcessSave();
            save_notice_sent = 0U;
        }
        else
        {
            if ((uint32_t)(now - save_notice_started) > 3000U)
            {
                LeverCal_SaveNoticeTimedOut();
                save_notice_sent = 0U;
            }
            return;
        }
    }
    else { save_notice_sent = 0U; }
    if (!USBD_HID_CanSendReport(&hUsbDeviceFS)) { return; }


    /*
     * 一帧包含当前全部状态：
     *
     * Lever
     * Buttons
     *
     * 即使没有发生变化，也持续发送。
     */
    IO4_SendInputReport();
}


/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  HAL_Init();

  /* USER CODE BEGIN Init */
  LeverCal_Init();

  /* USER CODE END Init */

  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_ADC1_Init();
  MX_USB_DEVICE_Init();

  /* USER CODE BEGIN 2 */

  /*
   * 启动ADC Continuous + DMA
   */
  if (HAL_ADC_Start_DMA(&hadc1,
                        (uint32_t *)&adc_raw,
                        1) != HAL_OK)
  {
      Error_Handler();
  }

  /* DMA keeps refreshing adc_raw in hardware. Avoid an interrupt for every
     conversion: no HT/TC callbacks consume this single-word circular buffer.
     Transfer/error interrupts remain enabled. */
  __HAL_DMA_DISABLE_IT(&hdma_adc1, DMA_IT_HT | DMA_IT_TC);

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */

  while (1)
  {
      IO4_Task();

    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */

      /*
       * 每 100ms 非阻塞刷新一次 USB OUT 统计，供调试器在线观察。
       */
      if ((HAL_GetTick() - last_debug_tick) >= 100U)
      {
          last_debug_tick = HAL_GetTick();

          USBD_HID_GetIO4Stats((USBD_HID_IO4StatsTypeDef *)&io4_debug_stats);
      }
  }

  /* USER CODE END 3 */
}


/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /**
    * Configure the main internal regulator output voltage
    */
  __HAL_RCC_PWR_CLK_ENABLE();

  __HAL_PWR_VOLTAGESCALING_CONFIG(
      PWR_REGULATOR_VOLTAGE_SCALE1);


  /**
    * Initializes RCC Oscillators
    */
  RCC_OscInitStruct.OscillatorType =
      RCC_OSCILLATORTYPE_HSE;

  RCC_OscInitStruct.HSEState =
      RCC_HSE_ON;

  RCC_OscInitStruct.PLL.PLLState =
      RCC_PLL_ON;

  RCC_OscInitStruct.PLL.PLLSource =
      RCC_PLLSOURCE_HSE;

  RCC_OscInitStruct.PLL.PLLM = 25;

  RCC_OscInitStruct.PLL.PLLN = 192;

  RCC_OscInitStruct.PLL.PLLP =
      RCC_PLLP_DIV2;

  RCC_OscInitStruct.PLL.PLLQ = 4;


  if (HAL_RCC_OscConfig(
          &RCC_OscInitStruct) != HAL_OK)
  {
      Error_Handler();
  }


  /**
    * Initializes CPU, AHB and APB clocks
    */
  RCC_ClkInitStruct.ClockType =
      RCC_CLOCKTYPE_HCLK |
      RCC_CLOCKTYPE_SYSCLK |
      RCC_CLOCKTYPE_PCLK1 |
      RCC_CLOCKTYPE_PCLK2;

  RCC_ClkInitStruct.SYSCLKSource =
      RCC_SYSCLKSOURCE_PLLCLK;

  RCC_ClkInitStruct.AHBCLKDivider =
      RCC_SYSCLK_DIV1;

  RCC_ClkInitStruct.APB1CLKDivider =
      RCC_HCLK_DIV2;

  RCC_ClkInitStruct.APB2CLKDivider =
      RCC_HCLK_DIV1;


  if (HAL_RCC_ClockConfig(
          &RCC_ClkInitStruct,
          FLASH_LATENCY_3) != HAL_OK)
  {
      Error_Handler();
  }
}


/**
  * @brief ADC1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_ADC1_Init(void)
{

  ADC_ChannelConfTypeDef sConfig = {0};


  /**
    * Configure ADC global features
    */
  hadc1.Instance = ADC1;

  hadc1.Init.ClockPrescaler =
      ADC_CLOCK_SYNC_PCLK_DIV4;

  hadc1.Init.Resolution =
      ADC_RESOLUTION_12B;

  hadc1.Init.ScanConvMode =
      DISABLE;

  hadc1.Init.ContinuousConvMode =
      ENABLE;

  hadc1.Init.DiscontinuousConvMode =
      DISABLE;

  hadc1.Init.ExternalTrigConvEdge =
      ADC_EXTERNALTRIGCONVEDGE_NONE;

  hadc1.Init.ExternalTrigConv =
      ADC_SOFTWARE_START;

  hadc1.Init.DataAlign =
      ADC_DATAALIGN_RIGHT;

  hadc1.Init.NbrOfConversion = 1;

  hadc1.Init.DMAContinuousRequests =
      ENABLE;

  hadc1.Init.EOCSelection =
      ADC_EOC_SINGLE_CONV;


  if (HAL_ADC_Init(&hadc1) != HAL_OK)
  {
      Error_Handler();
  }


  /**
    * ADC Channel 0
    */
  sConfig.Channel =
      ADC_CHANNEL_0;

  sConfig.Rank = 1;

  sConfig.SamplingTime =
      ADC_SAMPLETIME_84CYCLES;


  if (HAL_ADC_ConfigChannel(
          &hadc1,
          &sConfig) != HAL_OK)
  {
      Error_Handler();
  }
}


/**
  * Enable DMA controller clock
  */
static void MX_DMA_Init(void)
{

  __HAL_RCC_DMA2_CLK_ENABLE();


  /*
   * DMA2_Stream0 interrupt configuration
   */
  HAL_NVIC_SetPriority(
      DMA2_Stream0_IRQn,
      0,
      0);

  HAL_NVIC_EnableIRQ(
      DMA2_Stream0_IRQn);
}


/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};


  /* GPIO Ports Clock Enable */

  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();


  /*
   * KEY_A
   * KEY_S
   * KEY_D
   * KEY_J
   * KEY_K
   */
  GPIO_InitStruct.Pin =
      KEY_A_Pin |
      KEY_S_Pin |
      KEY_D_Pin |
      KEY_J_Pin |
      KEY_K_Pin;

  GPIO_InitStruct.Mode =
      GPIO_MODE_INPUT;

  GPIO_InitStruct.Pull =
      GPIO_PULLUP;

  HAL_GPIO_Init(
      GPIOA,
      &GPIO_InitStruct);


  /*
   * KEY_L
   * KEY_U
   * KEY_O
   * MOUSE_L
   * MOUSE_R
   */
  GPIO_InitStruct.Pin =
      KEY_L_Pin |
      KEY_U_Pin |
      KEY_O_Pin |
      MOUSE_L_Pin |
      MOUSE_R_Pin;

  GPIO_InitStruct.Mode =
      GPIO_MODE_INPUT;

  GPIO_InitStruct.Pull =
      GPIO_PULLUP;

  HAL_GPIO_Init(
      GPIOB,
      &GPIO_InitStruct);
}


/* USER CODE BEGIN 4 */

/* USER CODE END 4 */


/**
  * @brief This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */

  __disable_irq();

  while (1)
  {
  }

  /* USER CODE END Error_Handler_Debug */
}


#ifdef USE_FULL_ASSERT

/**
  * @brief Reports the name of the source file and
  *        source line number where assert_param failed.
  * @param file: source file name
  * @param line: line number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */

  /* User can add implementation here */

  /* USER CODE END 6 */
}

#endif /* USE_FULL_ASSERT */
