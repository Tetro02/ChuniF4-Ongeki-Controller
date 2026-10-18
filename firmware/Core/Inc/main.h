/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32f4xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define KEY_A_Pin GPIO_PIN_3
#define KEY_A_GPIO_Port GPIOA
#define KEY_S_Pin GPIO_PIN_4
#define KEY_S_GPIO_Port GPIOA
#define KEY_D_Pin GPIO_PIN_5
#define KEY_D_GPIO_Port GPIOA
#define KEY_J_Pin GPIO_PIN_6
#define KEY_J_GPIO_Port GPIOA
#define KEY_K_Pin GPIO_PIN_7
#define KEY_K_GPIO_Port GPIOA
#define KEY_L_Pin GPIO_PIN_0
#define KEY_L_GPIO_Port GPIOB
#define KEY_U_Pin GPIO_PIN_1
#define KEY_U_GPIO_Port GPIOB
/* RM: 原本在 PB2，但 PB2 是 STM32F4 的 BOOT1，板上带外部 10k 下拉，
   内部 40k 上拉敌不过它，引脚恒读低电平 -> RM 永远显示按下。
   已改到 PB5（纯 GPIO，无复用冲突）。改回 PB2 会重现该问题。 */
#define KEY_O_Pin GPIO_PIN_5
#define KEY_O_GPIO_Port GPIOB
#define MOUSE_L_Pin GPIO_PIN_10
#define MOUSE_L_GPIO_Port GPIOB
#define MOUSE_R_Pin GPIO_PIN_12
#define MOUSE_R_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
