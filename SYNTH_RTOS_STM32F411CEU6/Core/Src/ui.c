/*
 * ui.c
 *
 *  Created on: Jan 14, 2026
 *      Author: 환중
 */

#include "ui.h"
#include "main.h"
#include "ILI9341_STM32_Driver.h"
#include "ILI9341_GFX.h"
#include <string.h>
#include <math.h>

TaskHandle_t lcdTaskHandle = NULL;
QueueHandle_t lcdQueueHandle = NULL;
LcdState_t currentLcdState = LCD_STATE_INIT;

uint8_t sin_samples[1024];
static uint8_t prev_y[240] = { 0 };

static void LCD_Task(void *argument);
static void Generate_Sine_Samples(void);
static void draw_main_dashboard(void);
static void draw_system_info(void);
static void draw_moving_sine(uint8_t *data, uint32_t offset);

void display_init(void) {
	Generate_Sine_Samples();

	HAL_GPIO_WritePin(LCD_RST_PORT, LCD_RST_PIN, GPIO_PIN_RESET);
	HAL_Delay(100);
	HAL_GPIO_WritePin(LCD_RST_PORT, LCD_RST_PIN, GPIO_PIN_SET);
	HAL_Delay(100);

	ILI9341_Init();
}

void UI_Init(void) {
	lcdQueueHandle = xQueueCreate(5, sizeof(uint32_t));
	if (lcdQueueHandle == NULL) {
		Error_Handler();
	}

	BaseType_t result = xTaskCreate(LCD_Task, "LCDTask", 2048,
	NULL,
	tskIDLE_PRIORITY + 1, &lcdTaskHandle);

	if (result != pdPASS) {
		Error_Handler();
	}
}

static void Generate_Sine_Samples(void) {
	for (int i = 0; i < 1024; i++) {
		sin_samples[i] =
				(uint8_t) (120 + 50 * sin(5.0 * 2 * M_PI * i / 1024.0));
	}
}

static void draw_main_dashboard(void) {
	ILI9341_Fill_Screen(BLACK);
	vTaskDelay(pdMS_TO_TICKS(10));

	ILI9341_Draw_Filled_Rectangle_Coord(0, 0, 240, 30, GREEN);
	ILI9341_Draw_Text("SYSTEM READY", 55, 10, BLACK, 2, GREEN);

	ILI9341_Draw_Text("Welcome to", 30, 60, WHITE, 2, BLACK);
	ILI9341_Draw_Text("SYNTH RTOS", 20, 90, YELLOW, 3, BLACK);
	ILI9341_Draw_Text("BLACKPILL", 10, 130, CYAN, 3, BLACK);

	ILI9341_Draw_Horizontal_Line(20, 180, 200, WHITE);
	ILI9341_Draw_Text("Press Button", 60, 195, LIGHTGREY, 1, BLACK);
}

static void draw_system_info(void) {
	ILI9341_Fill_Screen(BLACK);
	ILI9341_Draw_Filled_Rectangle_Coord(0, 0, 240, 30, BLUE);
	ILI9341_Draw_Text("SYSTEM INFO", 60, 10, WHITE, 2, BLUE);

	ILI9341_Draw_Text("CPU: STM32F429", 20, 60, WHITE, 2, BLACK);
	ILI9341_Draw_Text("RTOS: FreeRTOS", 20, 90, WHITE, 2, BLACK);
	ILI9341_Draw_Text("LCD: ILI9341", 20, 120, WHITE, 2, BLACK);

	ILI9341_Draw_Text("Status: Running", 20, 160, GREEN, 2, BLACK);
}

static void draw_moving_sine(uint8_t *data, uint32_t offset) {
	for (int x = 0; x < 239; x++) {
		ILI9341_Draw_Vertical_Line(x, 50, 160, BLACK);

		if (x % 4 == 0) {
			ILI9341_Draw_Pixel(x, 120, DARKGREY);
		}

		uint32_t index = (offset + x) % 1024;
		uint8_t y_val = data[index];

		if (y_val < 50)
			y_val = 50;
		if (y_val > 210)
			y_val = 210;

		ILI9341_Draw_Pixel(x, y_val, YELLOW);

		if (x % 20 == 0) {
			vTaskDelay(pdMS_TO_TICKS(1));
		}
	}
}

static void LCD_Task(void *argument) {
	uint32_t received;
	uint32_t offset = 0;
	uint8_t *current_data_ptr = NULL;
	uint8_t screen_mode = 0;
	uint8_t last_screen_mode = 255;

	for (;;) {
		if (xQueueReceive(lcdQueueHandle, &received, 0) == pdTRUE) {
			if (received >= 0x20000000) {
				current_data_ptr = (uint8_t*) received;
				screen_mode = 2;
			} else {
				screen_mode = (uint8_t) received;
				current_data_ptr = NULL;
			}

			ILI9341_Fill_Screen(BLACK);
			memset(prev_y, 120, sizeof(prev_y));
		}

		if (screen_mode == 2 && current_data_ptr != NULL) {
			draw_moving_sine(current_data_ptr, offset);
			offset = (offset + 6) % 1024;
			vTaskDelay(pdMS_TO_TICKS(30));
		} else if (screen_mode != last_screen_mode) {
			if (screen_mode == 0)
				draw_main_dashboard();
			else if (screen_mode == 1)
				draw_system_info();
			last_screen_mode = screen_mode;
		}

		vTaskDelay(pdMS_TO_TICKS(30));
	}
}

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_PIN) {
	static uint32_t last_tick = 0;

	if (GPIO_PIN == Rotary2_KEY_Pin) {
		if (HAL_GetTick() - last_tick < 250)
			return;
		last_tick = HAL_GetTick();

		if (currentLcdState == LCD_STATE_MAIN_DASH)
			currentLcdState = LCD_STATE_SUB_INFO;
		else if (currentLcdState == LCD_STATE_SUB_INFO)
			currentLcdState = LCD_STATE_GRAPH_VIEW;
		else
			currentLcdState = LCD_STATE_MAIN_DASH;

		BaseType_t xHigherPriorityTaskWoken = pdFALSE;

		if (currentLcdState == LCD_STATE_GRAPH_VIEW) {
			uint32_t sin_addr = (uint32_t) sin_samples;
			xQueueSendFromISR(lcdQueueHandle, &sin_addr,
					&xHigherPriorityTaskWoken);
		} else {
			uint32_t state = (uint32_t) currentLcdState;
			xQueueSendFromISR(lcdQueueHandle, &state,
					&xHigherPriorityTaskWoken);
		}

		portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
	}
}
