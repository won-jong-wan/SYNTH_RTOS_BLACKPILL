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
#include <stdio.h>
#include "user_rtos.h"

TaskHandle_t lcdTaskHandle = NULL;
QueueHandle_t lcdQueueHandle = NULL;

LcdState_t currentLcdState = LCD_STATE_INIT; // 화면 초기 상태

// 버튼 누를 때마다 증가: 0(A) → 1(D) → 2(S) → 3(R) → 4(파형) → 5(RESO) → 6(CUTOFF) → 7(Vol)
uint8_t selected_adsr_idx = 0;

volatile uint8_t volume_val = 0;	// 볼륨 초기 값
volatile uint8_t selected_wave_type = 0; // 0:SINE, 1:SQUARE, 2:TRIANGLE -> 파형의 종류
const char *wave_names[] = { "SINE", "SQUARE", "TRIANGLE" };

// 샘플 배열 선언
uint8_t adsr_samples[1024];	// ADSR 그래프 배열
uint8_t sin_samples[1024];	// SIN 파형 배열

static void LCD_Task(void *argument);	// LCD 출력
static void Generate_ADSR_Samples(void);	// ADSR 그래프 생성
static void Generate_Sine_Samples(void);	// SIN 파형 생성
static void draw_dual_graph(uint8_t force_clear); // ADSR + SIN 출력 화면

void display_init(void) {
	Generate_ADSR_Samples();
	Generate_Sine_Samples();

	HAL_GPIO_WritePin(LCD_RST_PORT, LCD_RST_PIN, GPIO_PIN_RESET);
	HAL_Delay(100);
	HAL_GPIO_WritePin(LCD_RST_PORT, LCD_RST_PIN, GPIO_PIN_SET);
	HAL_Delay(100);

	ILI9341_Init();
	ILI9341_Fill_Screen(BLACK);
}

void UI_Init(void) {
	// 3. 큐 생성 및 초기화
	lcdQueueHandle = xQueueCreate(5, sizeof(uint32_t));
	if (lcdQueueHandle == NULL) {
		Error_Handler();
	}

	xQueueReset(lcdQueueHandle); // 부팅 시 쌓인 노이즈 제거

	// 4. 초기 상태 설정
	// 이 값이 MAIN_DASH여야 EXTI 콜백의 첫 번째 if문이 동작합니다.
	selected_adsr_idx = 0;

	// 5. Task 생성
	BaseType_t result = xTaskCreate(LCD_Task, "LCDTask", 2048,
	NULL,
	tskIDLE_PRIORITY + 2, &lcdTaskHandle);

	if (result != pdPASS) {
		Error_Handler();
	}
}

// SIN 파형 생성 부분
// 1024개의 값을 만들어서 점 생성
// 5번 물결치는 SIN 파형 생성
static void Generate_Sine_Samples(void) {
	for (int i = 0; i < 1024; i++) {
		sin_samples[i] = (uint8_t) (127
				+ 100 * sin(2.0 * M_PI * i / 1024.0 * 5.0));
	}
}

// ADSR 그래프 생성 부분
static void Generate_ADSR_Samples(void) {
	// 값 직접 임의로 설정해서 출력
	uint16_t attack_len = 100; // 작을수록 급격히 상승 (0~1024)
	uint16_t decay_len = 200; // 피크에서 유지 레벨까지 내려오는 시간
	uint16_t sustain_len = 400; // 유지되는 시간
	uint16_t release_len = 324; // 건반을 뗐을 때 사라지는 시간 (총합 1024 권장)

	uint8_t peak_level = 240; // Attack의 정점 높이 (0~255)
	uint8_t sustain_level = 120; // Sustain 구간의 높이 (0~peak_level)
	uint8_t base_level = 30;  // 바닥 높이
	uint16_t idx = 0;

	// ADSR 값 계산
	for (int i = 0; i < attack_len && idx < 1024; i++, idx++)
		adsr_samples[idx] = base_level
				+ (peak_level - base_level) * i / attack_len;
	for (int i = 0; i < decay_len && idx < 1024; i++, idx++)
		adsr_samples[idx] = peak_level
				- (peak_level - sustain_level) * i / decay_len;
	for (int i = 0; i < sustain_len && idx < 1024; i++, idx++)
		adsr_samples[idx] = sustain_level;
	for (int i = 0; i < release_len && idx < 1024; i++, idx++)
		adsr_samples[idx] = sustain_level
				- (sustain_level - base_level) * i / release_len;
	while (idx < 1024)
		adsr_samples[idx++] = base_level;
}

static void draw_dual_graph(uint8_t force_clear) {
	// [1] 음계 및 옥타브 관리 변수 (static)
	static int current_note_idx = 0;  // 음계 : C=0, D=1, E=2, F=3, G=4, A=5, B=6
	static int current_octave = 2;		// 옥타브 : 2 ~ 3
	static int last_idx_for_note = -1; // 이전 선택 인덱스 저장용

	// [2] 스위치를 누를 때마다 음계 상승
	if (selected_adsr_idx != last_idx_for_note) {
		current_note_idx++;
		if (current_note_idx > 6) {
			current_note_idx = 0;
			current_octave = (current_octave == 2) ? 3 : 2; // 2옥타브 <-> 3옥타브 순환
		}
		last_idx_for_note = selected_adsr_idx;
	}

	// 화면 전체 지우기
	if (force_clear) {
		// 화면 깨끗이 지워줌
		ILI9341_Fill_Screen(BLACK);
//		vTaskDelay(pdMS_TO_TICKS(5));

		// [상단] ADSR 타이틀 및 구분선
		// ILI9341_Draw_Horizontal_Line : 가로줄 그리는 함수
		// ILI9341_Draw_Text : 문구 작성하는 함수
		ILI9341_Draw_Filled_Rectangle_Coord(0, 0, 240, 30, CYAN);
		ILI9341_Draw_Text("ADSR ENVELOPE", 42, 8, BLACK, 2, CYAN);
		ILI9341_Draw_Horizontal_Line(0, 30, 240, WHITE);
	}

	// ADSR 그래프 (노란색)
	for (int x = 0; x < 240; x++) {
		uint32_t index = (x * 1024) / 240;
		int16_t y_raw = 110 - ((adsr_samples[index] * 60) / 255);
		ILI9341_Draw_Pixel(x, (uint16_t) y_raw, YELLOW);
	}

	// [하단] 고정 파형 그래프 (Y=295 유지)
	for (int x = 0; x < 240; x++) {
		uint32_t index = (x * 1024) / 240;
		uint8_t sample_val = sin_samples[index];
		int16_t y_raw = 295 - ((sample_val * 50) / 255);
		if (y_raw > 318)
			y_raw = 318;
		ILI9341_Draw_Pixel(x, (uint16_t) y_raw, GREEN);
	}

	// 1. ADSR 라벨 (A, D, S, R)
	// 스위치를 눌러서 문자를 선택
	// 선택했을 때 빨간색으로 변경됨
	uint16_t adrs_y = 135;
//	ILI9341_Draw_Filled_Rectangle_Coord(0, 130, 240, 153, BLACK);
	ILI9341_Draw_Text("A", 30, adrs_y, (selected_adsr_idx == 0 ? RED : WHITE),
			2, BLACK);
	ILI9341_Draw_Text("D", 85, adrs_y, (selected_adsr_idx == 1 ? RED : WHITE),
			2, BLACK);
	ILI9341_Draw_Text("S", 140, adrs_y, (selected_adsr_idx == 2 ? RED : WHITE),
			2, BLACK);
	ILI9341_Draw_Text("R", 195, adrs_y, (selected_adsr_idx == 3 ? RED : WHITE),
			2, BLACK);

	// 2. 파형 타이틀 바 + 수동 음계 표시 (우측 정렬)
	uint16_t wave_bar_color = (selected_adsr_idx == 4 ? RED : MAGENTA);
	ILI9341_Draw_Filled_Rectangle_Coord(0, 160, 240, 185, wave_bar_color);

	// 파형 이름 (왼쪽 끝)
	ILI9341_Draw_Text(wave_names[selected_wave_type], 5, 165, WHITE, 2,
			wave_bar_color);

	// 음계 표시 (노란색 고정)
	const char *notes[] = { "C", "D", "E", "F", "G", "A", "B" };
	char note_buf[12];
	sprintf(note_buf, "NOTE:%s%d", notes[current_note_idx], current_octave);
	ILI9341_Draw_Text(note_buf, 145, 165, YELLOW, 2, wave_bar_color);

	// 3. 필터 라벨 (RESO, CUTOFF)
	// ILI9341_Draw_Filled_Rectangle_Coord : 사각형 그리는 함수
	uint16_t filter_y = 195;
//	ILI9341_Draw_Filled_Rectangle_Coord(0, 190, 240, 215, BLACK);
	ILI9341_Draw_Text("RESO", 45, filter_y,
			(selected_adsr_idx == 5 ? RED : WHITE), 2, BLACK);
	ILI9341_Draw_Text("CUTOFF", 125, filter_y,
			(selected_adsr_idx == 6 ? RED : WHITE), 2, BLACK);

	// 4. 볼륨 게이지
	uint16_t vol_y = 220;
	uint16_t vol_color = (selected_adsr_idx == 7 ? RED : WHITE);
//	ILI9341_Draw_Filled_Rectangle_Coord(0, vol_y - 2, 240, vol_y + 12, BLACK);
	ILI9341_Draw_Text("Vol", 15, vol_y, vol_color, 1, BLACK);

	// 볼륨 게이지를 막대 그래프로 표현
	// i = 0~9까지 10번 반복 (막대 10개)
	// volume_val을 10으로 나눈 값과 i를 비교해서
	// i가 더 작으면 게이지 한 칸씩 채움
	for (int i = 0; i < 10; i++) {
		uint16_t bar_x = 45 + (i * 17);
		if (i < (volume_val / 10)) {
			ILI9341_Draw_Filled_Rectangle_Coord(bar_x, vol_y, bar_x + 13,
					vol_y + 8, vol_color);
		} else {
			ILI9341_Draw_Rectangle(bar_x, vol_y, 13, 8, DARKGREY);
		}
	}
	// ILI9341_Draw_Rectangle : 테두리만 있는 사각형 그리는 함수

	// 10 ~ 100 값을 출력
	char vol_buf[5];
	sprintf(vol_buf, "%d", volume_val);
	ILI9341_Draw_Text(vol_buf, 215, vol_y, vol_color, 1, BLACK);
}

static void LCD_Task(void *argument) {
	uint32_t received;

	// 시작 시 메인 화면 그리기
	draw_dual_graph(1);
//	LcdState_t lastState = LCD_STATE_ADSR_VIEW;

	for (;;) {
		if (xQueueReceive(lcdQueueHandle, &received, portMAX_DELAY) == pdTRUE) {

			volume_val = g_enc_pos[1];

			draw_dual_graph(0);

			vTaskDelay(pdMS_TO_TICKS(50));

			uint32_t msg = (uint32_t) currentLcdState;
			BaseType_t xHigherPriorityTaskWoken = pdFALSE;
			xQueueSendFromISR(lcdQueueHandle, &msg, &xHigherPriorityTaskWoken);
			portYIELD()
		}
	}
}

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_PIN) {
	static uint32_t last_tick = 0;

	uint32_t now = HAL_GetTick();

	if (GPIO_PIN == Rotary1_KEY_Pin) {
		// 1초동안 버튼 무시
		// 떨림으로 버튼이 눌리는 것을 방지
		if (now < 1000)
			return;

		// 여러번 눌리는 것을 방지
		if (now - last_tick < 250)
			return;
		last_tick = now;

//		// 메인 화면에서 누르면 그래프 출력 화면으로 전환
//		if (currentLcdState == LCD_STATE_MAIN_DASH) {
//			currentLcdState = LCD_STATE_ADSR_VIEW;
//			selected_adsr_idx = 0;
//		}

		// 그래프 화면에서 버튼을 눌렀을 때
		// 버튼을 눌렀을 때 다음 항목으로 넘어가지 않는 파형, 볼륨 if
		// 한 번 누를 때마다 다음 항목으로 넘어가는 것들은 else
//		else if (currentLcdState == LCD_STATE_ADSR_VIEW) {
		if (selected_adsr_idx == 4) { // 1. 파형 선택 바
			selected_wave_type++;
			if (selected_wave_type > 2) {
				selected_wave_type = 0;
				selected_adsr_idx = 5; // 다음: RESO로 이동
			}
		} else if (selected_adsr_idx == 6) { // 2. 볼륨 조절 모드 (마지막 단계)
			selected_adsr_idx = 0; // 다시 처음: Attack(A)으로 복귀

		} else {
			// 0~3(ADSR), 5(RESO), 6(CUTOFF)는 단순히 다음 인덱스로 이동
			selected_adsr_idx++;
		}
//		}

		// 인터럽트 처리
		// 일해라!!
		uint32_t msg = (uint32_t) currentLcdState;
		BaseType_t xHigherPriorityTaskWoken = pdFALSE;
		xQueueSendFromISR(lcdQueueHandle, &msg, &xHigherPriorityTaskWoken);
		portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
	}
}
