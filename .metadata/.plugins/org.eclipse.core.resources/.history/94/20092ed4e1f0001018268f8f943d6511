/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
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
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "string.h"
#include "cmsis_os.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "ILI9341_STM32_Driver.h"
#include "ILI9341_GFX.h"
#include "math.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
typedef enum{
	LCD_STATE_INIT = 0,
	LCD_STATE_MAIN_DASH,   // 기존 텍스트 화면
	LCD_STATE_SUB_INFO,    // 새로운 정보 화면(제외해도 됨)
	LCD_STATE_GRAPH_VIEW   // 그래프 화면
}LcdState_t;
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

ETH_TxPacketConfig TxConfig;
ETH_DMADescTypeDef  DMARxDscrTab[ETH_RX_DESC_CNT]; /* Ethernet Rx DMA Descriptors */
ETH_DMADescTypeDef  DMATxDscrTab[ETH_TX_DESC_CNT]; /* Ethernet Tx DMA Descriptors */

ETH_HandleTypeDef heth;

SPI_HandleTypeDef hspi5;

UART_HandleTypeDef huart3;

PCD_HandleTypeDef hpcd_USB_OTG_FS;

/* Definitions for LCDTask */
osThreadId_t LCDTaskHandle;
const osThreadAttr_t LCDTask_attributes = {
  .name = "LCDTask",
  .stack_size = 2048 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for lcdQueue */
osMessageQueueId_t lcdQueueHandle;
const osMessageQueueAttr_t lcdQueue_attributes = {
  .name = "lcdQueue"
};
/* USER CODE BEGIN PV */
LcdState_t currentLcdState = LCD_STATE_INIT;
static uint8_t sin_samples[1024]; // 1024개 샘플 저장소
static uint8_t prev_y[240] = {0};
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_ETH_Init(void);
static void MX_USART3_UART_Init(void);
static void MX_USB_OTG_FS_PCD_Init(void);
static void MX_SPI5_Init(void);
void LCD_Task(void *argument);

/* USER CODE BEGIN PFP */
void draw_main_dashboard(void);	 // 메인 대시보드 화면 그리기
void draw_system_info(void); // 시스템 정보 화면 그리기
void draw_moving_sine(uint8_t *data, uint32_t offset); // 사인파 애니메이션 그리기
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

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

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_ETH_Init();
  MX_USART3_UART_Init();
  MX_USB_OTG_FS_PCD_Init();
  MX_SPI5_Init();
  /* USER CODE BEGIN 2 */

  // LCD 하드웨어 리셋
  HAL_GPIO_WritePin(GPIOD, RESET_Pin, GPIO_PIN_RESET);
  HAL_Delay(100);
  HAL_GPIO_WritePin(GPIOD, RESET_Pin, GPIO_PIN_SET);
  HAL_Delay(100);

  // 사인파 샘플링 (1024개 생성)
  // Y = 120 + 50 * sin(x)
  // 중심: 120 (화면 중앙)
  // 진폭: 50 (위아래로 50픽셀)
  // 주기: 1024개 샘플 안에 5개의 사인파
  for (int i = 0; i < 1024; i++) {
      sin_samples[i] = (uint8_t)(120 + 50 * sin(5.0 * 2 * M_PI * i / 1024.0));
  }

  // 2. 라이브러리 초기화
  ILI9341_Init();

  // 3. 테스트 화면 출력
  /* USER CODE END 2 */

  /* Init scheduler */
  osKernelInitialize();

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* Create the queue(s) */
  /* creation of lcdQueue */
  lcdQueueHandle = osMessageQueueNew (5, sizeof(uint32_t), &lcdQueue_attributes);

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of LCDTask */
  LCDTaskHandle = osThreadNew(LCD_Task, NULL, &LCDTask_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  /* add threads, ... */
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */

  /* Start scheduler */
  osKernelStart();

  /* We should never get here as control is now taken by the scheduler */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
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

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_BYPASS;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 4;
  RCC_OscInitStruct.PLL.PLLN = 168;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 7;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief ETH Initialization Function
  * @param None
  * @retval None
  */
static void MX_ETH_Init(void)
{

  /* USER CODE BEGIN ETH_Init 0 */

  /* USER CODE END ETH_Init 0 */

   static uint8_t MACAddr[6];

  /* USER CODE BEGIN ETH_Init 1 */

  /* USER CODE END ETH_Init 1 */
  heth.Instance = ETH;
  MACAddr[0] = 0x00;
  MACAddr[1] = 0x80;
  MACAddr[2] = 0xE1;
  MACAddr[3] = 0x00;
  MACAddr[4] = 0x00;
  MACAddr[5] = 0x00;
  heth.Init.MACAddr = &MACAddr[0];
  heth.Init.MediaInterface = HAL_ETH_RMII_MODE;
  heth.Init.TxDesc = DMATxDscrTab;
  heth.Init.RxDesc = DMARxDscrTab;
  heth.Init.RxBuffLen = 1524;

  /* USER CODE BEGIN MACADDRESS */

  /* USER CODE END MACADDRESS */

  if (HAL_ETH_Init(&heth) != HAL_OK)
  {
    Error_Handler();
  }

  memset(&TxConfig, 0 , sizeof(ETH_TxPacketConfig));
  TxConfig.Attributes = ETH_TX_PACKETS_FEATURES_CSUM | ETH_TX_PACKETS_FEATURES_CRCPAD;
  TxConfig.ChecksumCtrl = ETH_CHECKSUM_IPHDR_PAYLOAD_INSERT_PHDR_CALC;
  TxConfig.CRCPadCtrl = ETH_CRC_PAD_INSERT;
  /* USER CODE BEGIN ETH_Init 2 */

  /* USER CODE END ETH_Init 2 */

}

/**
  * @brief SPI5 Initialization Function
  * @param None
  * @retval None
  */
static void MX_SPI5_Init(void)
{

  /* USER CODE BEGIN SPI5_Init 0 */

  /* USER CODE END SPI5_Init 0 */

  /* USER CODE BEGIN SPI5_Init 1 */

  /* USER CODE END SPI5_Init 1 */
  /* SPI5 parameter configuration*/
  hspi5.Instance = SPI5;
  hspi5.Init.Mode = SPI_MODE_MASTER;
  hspi5.Init.Direction = SPI_DIRECTION_2LINES;
  hspi5.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi5.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi5.Init.CLKPhase = SPI_PHASE_1EDGE;
  hspi5.Init.NSS = SPI_NSS_SOFT;
  hspi5.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_64;
  hspi5.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi5.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi5.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi5.Init.CRCPolynomial = 10;
  if (HAL_SPI_Init(&hspi5) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN SPI5_Init 2 */

  /* USER CODE END SPI5_Init 2 */

}

/**
  * @brief USART3 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART3_UART_Init(void)
{

  /* USER CODE BEGIN USART3_Init 0 */

  /* USER CODE END USART3_Init 0 */

  /* USER CODE BEGIN USART3_Init 1 */

  /* USER CODE END USART3_Init 1 */
  huart3.Instance = USART3;
  huart3.Init.BaudRate = 115200;
  huart3.Init.WordLength = UART_WORDLENGTH_8B;
  huart3.Init.StopBits = UART_STOPBITS_1;
  huart3.Init.Parity = UART_PARITY_NONE;
  huart3.Init.Mode = UART_MODE_TX_RX;
  huart3.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart3.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart3) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART3_Init 2 */

  /* USER CODE END USART3_Init 2 */

}

/**
  * @brief USB_OTG_FS Initialization Function
  * @param None
  * @retval None
  */
static void MX_USB_OTG_FS_PCD_Init(void)
{

  /* USER CODE BEGIN USB_OTG_FS_Init 0 */

  /* USER CODE END USB_OTG_FS_Init 0 */

  /* USER CODE BEGIN USB_OTG_FS_Init 1 */

  /* USER CODE END USB_OTG_FS_Init 1 */
  hpcd_USB_OTG_FS.Instance = USB_OTG_FS;
  hpcd_USB_OTG_FS.Init.dev_endpoints = 4;
  hpcd_USB_OTG_FS.Init.speed = PCD_SPEED_FULL;
  hpcd_USB_OTG_FS.Init.dma_enable = DISABLE;
  hpcd_USB_OTG_FS.Init.phy_itface = PCD_PHY_EMBEDDED;
  hpcd_USB_OTG_FS.Init.Sof_enable = ENABLE;
  hpcd_USB_OTG_FS.Init.low_power_enable = DISABLE;
  hpcd_USB_OTG_FS.Init.lpm_enable = DISABLE;
  hpcd_USB_OTG_FS.Init.vbus_sensing_enable = ENABLE;
  hpcd_USB_OTG_FS.Init.use_dedicated_ep1 = DISABLE;
  if (HAL_PCD_Init(&hpcd_USB_OTG_FS) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USB_OTG_FS_Init 2 */

  /* USER CODE END USB_OTG_FS_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOF_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOG_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, LD1_Pin|LD3_Pin|LD2_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOG, CS_Pin|USB_PowerSwitchOn_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOD, RESET_Pin|DC_RS_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin : USER_Btn_Pin */
  GPIO_InitStruct.Pin = USER_Btn_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(USER_Btn_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : LD1_Pin LD3_Pin LD2_Pin */
  GPIO_InitStruct.Pin = LD1_Pin|LD3_Pin|LD2_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure GPIO pins : CS_Pin USB_PowerSwitchOn_Pin */
  GPIO_InitStruct.Pin = CS_Pin|USB_PowerSwitchOn_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOG, &GPIO_InitStruct);

  /*Configure GPIO pin : USB_OverCurrent_Pin */
  GPIO_InitStruct.Pin = USB_OverCurrent_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(USB_OverCurrent_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : RESET_Pin DC_RS_Pin */
  GPIO_InitStruct.Pin = RESET_Pin|DC_RS_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

  /* EXTI interrupt init*/
  HAL_NVIC_SetPriority(EXTI15_10_IRQn, 5, 0);
  HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */
void draw_main_dashboard(void) {
    // 1. 그라데이션 대신 단색으로 먼저 테스트 (가장 안전)
    ILI9341_Fill_Screen(BLACK);
    osDelay(10);

    // 2. 상단바 (Y: 0 ~ 30)
    // ILI9341_Draw_Filled_Rectangle_Coord : 지정된 좌표로 채워진 사각형을 그리는 함수
    ILI9341_Draw_Filled_Rectangle_Coord(0, 0, 240, 30, GREEN);
    ILI9341_Draw_Text("SYSTEM READY", 55, 10, BLACK, 2, GREEN);

    // 3. 메인 텍스트 위치 대폭 수정 (Y좌표 200 아래로 고정)
    // Size 4는 가로폭(CHAR_WIDTH * 4)이 커서 X좌표 오버플로우 위험이 있으니
    // X좌표도 작게 시작하세요.
    ILI9341_Draw_Text("Welcome to", 30, 60, WHITE, 2, BLACK);
    ILI9341_Draw_Text("SYNTH RTOS", 20, 90, YELLOW, 3, BLACK);

    // BLACKPILL은 Size 3으로 낮춰서 테스트 (가로폭 오버플로우 방지)
    ILI9341_Draw_Text("BLACKPILL", 10, 130, CYAN, 3, BLACK);

    // 4. 하단 안내 (안전한 Y좌표 180)
    // ILI9341_Draw_Horizontal_Line : 수평선(가로선)을 그리는 함수
    ILI9341_Draw_Horizontal_Line(20, 180, 200, WHITE);
    ILI9341_Draw_Text("Press Button", 60, 195, LIGHTGREY, 1, BLACK);
}

void draw_system_info(void) {
    ILI9341_Fill_Screen(BLACK);
    ILI9341_Draw_Filled_Rectangle_Coord(0, 0, 240, 30, BLUE);
    ILI9341_Draw_Text("SYSTEM INFO", 60, 10, WHITE, 2, BLUE);

    ILI9341_Draw_Text("CPU: STM32F429", 20, 60, WHITE, 2, BLACK);
    ILI9341_Draw_Text("RTOS: CMSIS-V2", 20, 90, WHITE, 2, BLACK);
    ILI9341_Draw_Text("LCD: ILI9341", 20, 120, WHITE, 2, BLACK);

    ILI9341_Draw_Text("Status: Running", 20, 160, GREEN, 2, BLACK);
}

void draw_moving_sine(uint8_t *data, uint32_t offset) {
	// 화면 가로 240픽셀을 순회
    for(int x = 0; x < 239; x++) {
        // 세로 선으로 한 번에 지우기
    	// ILI9341_Draw_Vertical_Line : 세로선을 그리는 함수
        ILI9341_Draw_Vertical_Line(x, 50, 160, BLACK);

        // 가이드라인(중앙선)
        if(x % 4 == 0) {
            ILI9341_Draw_Pixel(x, 120, DARKGREY);
        }

        // 새로운 샘플값 읽기
        uint32_t index = (offset + x) % 1024;
        uint8_t y_val = data[index];

        // Y값 안전 범위 제한 (50~210)
        // LCD 컨트롤러 오작동 방지
        if(y_val < 50)  y_val = 50;
        if(y_val > 210) y_val = 210;

        // 노란색 점 찍기 (샘플 표현)
        ILI9341_Draw_Pixel(x, y_val, YELLOW);

        // 주기적으로 다른 Task에게 양보
        // 20픽셀마다 1ms 대기 → 시스템 안정성 확보
        if(x % 20 == 0) {
            osDelay(1);
        }
    }
}
/* USER CODE END 4 */

/* USER CODE BEGIN Header_LCD_Task */
/**
 * @brief Function implementing the defaultTask thread.
 * @param argument: Not used
 * @retval None
 */
/* USER CODE END Header_LCD_Task */
void LCD_Task(void *argument)
{
  /* USER CODE BEGIN 5 */
	uint32_t received;	// 큐에서 받은 데이터
    uint32_t offset = 0;	// 사인파 시작 인덱스 (애니메이션용)
    uint8_t *current_data_ptr = NULL;	// 현재 그릴 데이터 배열 포인터
    uint8_t screen_mode = 0;	// 현재 화면 모드 (0=대시보드, 1=정보, 2=그래프)
    uint8_t last_screen_mode = 255;	// 이전 화면 모드 (변경 감지용)
  /* Infinite loop */
  for(;;)
  {
	  // 1. 큐 확인 (즉시 확인)
	  if(osMessageQueueGet(lcdQueueHandle, &received, NULL, 0) == osOK) {
		  // 받은 값이 메모리 주소인지 화면 번호인지 구분
		  if(received >= 0x20000000) {
			  // 큰 숫자 = 메모리 주소 → 그래프 모드
			  current_data_ptr = (uint8_t*)received;
			  screen_mode = 2;
		  } else {
			  // 작은 숫자 = 화면 번호 (0 or 1)
			  screen_mode = (uint8_t)received;
			  current_data_ptr = NULL;
		  }
		  // // 화면 전환 시 한 번만 전체 화면 지우기
		  ILI9341_Fill_Screen(BLACK);
		  // 이전 좌표 배열 초기화 (잔상 방지)
		  memset(prev_y, 120, sizeof(prev_y));
	  }
	  // 2. 모드별 실행
	  // 그래프 모드 (애니메이션)
	  if(screen_mode == 2 && current_data_ptr != NULL) {
		  // offset을 6씩 증가 (파형이 왼쪽으로 이동하는 효과)
		  draw_moving_sine(current_data_ptr, offset);
		  offset = (offset + 6) % 1024;
		  osDelay(30);
		  }
	  else if(screen_mode != last_screen_mode) {
		  // 화면이 바뀌었을 때만 한 번 그리기
		  if(screen_mode == 0) draw_main_dashboard();
		  else if(screen_mode == 1) draw_system_info();

		  last_screen_mode = screen_mode;
	  }
	  osDelay(10); // 시스템 안정성
	  }
}

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_PIN)
{
	// 마지막 버튼 입력 시간 (채터링 방지)
	static uint32_t last_tick = 0;
	if(GPIO_PIN == USER_Btn_Pin)
	{
		if(HAL_GetTick() - last_tick < 250) return;
		last_tick = HAL_GetTick();

		// 1. 상태 순환 (DASH -> INFO -> GRAPH -> DASH)
		if(currentLcdState == LCD_STATE_MAIN_DASH)      currentLcdState = LCD_STATE_SUB_INFO;
		else if(currentLcdState == LCD_STATE_SUB_INFO)  currentLcdState = LCD_STATE_GRAPH_VIEW;
		else                                            currentLcdState = LCD_STATE_MAIN_DASH;

		// 2. 큐에 데이터 전송
		if(currentLcdState == LCD_STATE_GRAPH_VIEW) {
			// 그래프 상태일 때는 '주소'를 보냄 (0x20000000 이상인 큰 수)
			uint32_t sin_addr = (uint32_t)sin_samples;
			osMessageQueuePut(lcdQueueHandle, &sin_addr, 0, 0);
		} else {
			// 대시보드나 인포 상태일 때는 'Enum 상태값'을 보냄 (0, 1 같은 작은 수)
			osMessageQueuePut(lcdQueueHandle, &currentLcdState, 0, 0);
		}
	}
  /* USER CODE END 5 */
}

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
