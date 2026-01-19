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
#define Rotary2_S1_Pin GPIO_PIN_0
#define Rotary2_S1_GPIO_Port GPIOA
#define Rotary2_S2_Pin GPIO_PIN_1
#define Rotary2_S2_GPIO_Port GPIOA
#define Rotary2_KEY_Pin GPIO_PIN_2
#define Rotary2_KEY_GPIO_Port GPIOA
#define Rotary2_KEY_EXTI_IRQn EXTI2_IRQn
#define R4_Pin GPIO_PIN_0
#define R4_GPIO_Port GPIOB
#define R3_Pin GPIO_PIN_1
#define R3_GPIO_Port GPIOB
#define R1_Pin GPIO_PIN_12
#define R1_GPIO_Port GPIOB
#define C1_Pin GPIO_PIN_13
#define C1_GPIO_Port GPIOB
#define C4_Pin GPIO_PIN_8
#define C4_GPIO_Port GPIOA
#define C3_Pin GPIO_PIN_9
#define C3_GPIO_Port GPIOA
#define C2_Pin GPIO_PIN_10
#define C2_GPIO_Port GPIOA
#define DC_RS_Pin GPIO_PIN_15
#define DC_RS_GPIO_Port GPIOA
#define R2_Pin GPIO_PIN_4
#define R2_GPIO_Port GPIOB
#define RESET_Pin GPIO_PIN_5
#define RESET_GPIO_Port GPIOB
#define Rotary1_S1_Pin GPIO_PIN_6
#define Rotary1_S1_GPIO_Port GPIOB
#define Rotary1_S2_Pin GPIO_PIN_7
#define Rotary1_S2_GPIO_Port GPIOB
#define Rotary1_KEY_Pin GPIO_PIN_8
#define Rotary1_KEY_GPIO_Port GPIOB
#define Rotary1_KEY_EXTI_IRQn EXTI9_5_IRQn
#define CS_Pin GPIO_PIN_9
#define CS_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */
/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
