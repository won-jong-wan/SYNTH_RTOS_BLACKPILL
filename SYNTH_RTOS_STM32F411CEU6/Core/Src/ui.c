/*
 * ui.c
 *
 *  Created on: Jan 14, 2026
 *      Author: 영교
 */

#include "ui.h"
#include "main.h"
#include "ILI9341_STM32_Driver.h"
#include "ILI9341_GFX.h"
#include <string.h>
#include <math.h>
#include "task.h"
#include "user_rtos.h"

TaskHandle_t lcdTaskHandle = NULL;
QueueHandle_t lcdQueueHandle = NULL;
LcdState_t currentLcdState = LCD_STATE_GRAPH_VIEW;

#define LCD_W   240
#define LCD_H   320

uint8_t sin_samples[1024];
static uint8_t prev_y[240] = { 0 };

static void LCD_Task(void *argument);
static void Generate_Sine_Samples(void);
static void draw_main_dashboard(void);
static void draw_line(int x0, int y0, int x1, int y1, uint16_t color);
static void draw_moving_sine(uint8_t *data, uint32_t offset);
volatile UI_ADSR_t g_ui_adsr = { .attack_steps = 40, .decay_steps = 30,
		.sustain_level = 50, .release_steps = 60 };

volatile UI_Wave_t g_ui_wave = UI_WAVE_SINE;
volatile uint8_t g_ui_dirty = 1;

// ================== [추가 1] UI 편집 모드 + 선택 dirty ==================
typedef enum {
	UI_EDIT_ADSR = 0, UI_EDIT_FILTER, UI_EDIT_VOLUME
} UI_EditMode_t;

static void UI_SelectVolumeMode(void);

volatile UI_EditMode_t g_ui_edit_mode = UI_EDIT_ADSR;

// 선택(강조 박스)만 갱신하는 dirty
volatile uint8_t g_ui_adsr_sel_dirty = 1;
volatile uint8_t g_ui_filter_sel_dirty = 1;

// 엔코더/버튼 처리 함수(다른 파일에서도 쓸 수 있게 하려면 ui.h로 빼도 됨)
static void UI_MoveAdsrSelect(void);
static void UI_ToggleFilterSelect(void);
void UI_OnEncoderDelta(int delta);

static int clampi(int v, int lo, int hi);
// ================================================================

void display_init(void) {
	Generate_Sine_Samples();

	HAL_GPIO_WritePin(GPIOD, RESET_Pin, GPIO_PIN_RESET);
	HAL_Delay(100);
	HAL_GPIO_WritePin(GPIOD, RESET_Pin, GPIO_PIN_SET);
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

static void draw_adsr_labels_static(void);
static void draw_adsr_selection(void);

typedef enum {
	ADSR_SEL_A = 0, ADSR_SEL_D, ADSR_SEL_S, ADSR_SEL_R
} UI_ADSR_Select_t;

volatile UI_ADSR_Select_t g_adsr_sel = ADSR_SEL_A;

static void draw_filter_labels_static(void);
static void draw_filter_selection(void);

typedef enum {
	FILTER_SEL_CUTOFF = 0, FILTER_SEL_RESO
} UI_Filter_Select_t;

volatile UI_Filter_Select_t g_filter_sel = FILTER_SEL_CUTOFF;

// 사운드 엔진이 갱신해줄 UI 표시용 값
volatile uint8_t g_ui_note = 0;      // 0~11 (C~B) 혹은 너가 쓰는 인덱스
volatile uint8_t g_ui_oct = 4;      // 옥타브
volatile uint8_t g_ui_vol = 80;     // 0~100
volatile uint8_t g_ui_note_dirty = 1;
volatile uint8_t g_ui_vol_dirty = 1;

// FILTER 표시용 값 (0~100)
volatile uint8_t g_ui_cutoff = 50;   // 0~100
volatile uint8_t g_ui_reso = 30;   // 0~100

// 그래프 부분 갱신용 dirty
volatile uint8_t g_ui_adsr_dirty = 1;
volatile uint8_t g_ui_filter_dirty = 1;

static void draw_note_center(void);

static void draw_volume_bar(int top); // top=1이면 위, 0이면 아래

// ===== Layout constants =====
#define UI_M        6

// Volume bar (bottom)
#define VOL_H       8
#define VOL_Y0      (LCD_H - UI_M - VOL_H)
#define VOL_Y1      (VOL_Y0 + VOL_H - 1)

// Wave graph
#define WAVE_H      90
#define WAVE_Y1     (VOL_Y0 - UI_M - 1)
#define WAVE_Y0     (WAVE_Y1 - WAVE_H + 1)

// Filter label row (above wave)
#define FILTER_LH   22
#define FILTER_LY0  (WAVE_Y0 - FILTER_LH)
#define FILTER_LY1  (WAVE_Y0 - 1)

// ADSR graph (top)
#define ADSR_X0     10
#define ADSR_W      220
#define ADSR_Y0     UI_M
#define ADSR_H      108
#define ADSR_X1     (ADSR_X0 + ADSR_W - 1)
#define ADSR_Y1     (ADSR_Y0 + ADSR_H - 1)

// ADSR label row (below ADSR graph)
#define ADSR_LH     20
#define ADSR_LY0    (ADSR_Y1 + 4)
#define ADSR_LY1    (ADSR_LY0 + ADSR_LH - 1)

// Note area (between ADSR labels and FILTER labels)
#define NOTE_H      40
#define NOTE_Y0     (ADSR_LY1 + 6)
#define NOTE_Y1     (NOTE_Y0 + NOTE_H - 1)

#define WAVE_X0  10
#define WAVE_W   220
#define WAVE_X1  (WAVE_X0 + WAVE_W - 1)

#define VOL_X0  10
#define VOL_W   220
#define VOL_X1  (VOL_X0 + VOL_W - 1)

static void draw_adsr_graph(void) {
	// 영역: (10, 40) ~ (230, 150)
	int x0 = ADSR_X0, y0 = ADSR_Y0, w = ADSR_W, h = ADSR_H;

	// 프레임
	ILI9341_Draw_Hollow_Rectangle_Coord(ADSR_X0, ADSR_Y0, ADSR_X1, ADSR_Y1,
			WHITE);

	// ---- ADSR 파라미터를 "화면 폭"으로 매핑 ----
	// sustain은 "길이" 정보가 없으니, 보기 좋게 고정 폭(예: 전체의 25%)로 둠
	int wS = w / 4;

	// A/D/R은 상대 비율로 폭 계산
	uint32_t A = g_ui_adsr.attack_steps;
	uint32_t D = g_ui_adsr.decay_steps;
	uint32_t R = g_ui_adsr.release_steps;

	uint32_t denom = (A + D + R);
	if (denom == 0)
		denom = 1;

	int avail = (w - wS);
	int wA = (int) ((uint64_t) avail * A / denom);
	int wD = (int) ((uint64_t) avail * D / denom);
	int wR = (int) ((uint64_t) avail * R / denom);

	// 픽셀 보정(합이 w 넘치거나 모자랄 때)
	int sum = wA + wD + wS + wR;
	if (sum != w)
		wR += (w - sum);

	// Y 좌표 (위가 1.0, 아래가 0.0)
	int y_base = ADSR_Y1; // ✅ 프레임 안쪽 끝점
	int y_peak = ADSR_Y0;
	int h_pix = ADSR_H - 1; // ✅ 픽셀 높이(끝점 포함이라 -1)
	int y_sus = ADSR_Y0 + ((100 - g_ui_adsr.sustain_level) * h_pix) / 100;

	if (y_sus < ADSR_Y0)
		y_sus = ADSR_Y0;
	if (y_sus > ADSR_Y1)
		y_sus = ADSR_Y1;

	// ADSR 꼭짓점 좌표
	int xA0 = x0;
	int yA0 = y_base;
	int xA1 = x0 + wA;
	int yA1 = y_peak;

	int xD1 = xA1 + wD;
	int yD1 = y_sus;

	int xS1 = xD1 + wS;
	int yS1 = y_sus;

	int xR1 = xS1 + wR;
	int yR1 = y_base;

	if (xR1 > ADSR_X1)
		xR1 = ADSR_X1;
	if (xA1 > ADSR_X1)
		xA1 = ADSR_X1;
	if (xD1 > ADSR_X1)
		xD1 = ADSR_X1;
	if (xS1 > ADSR_X1)
		xS1 = ADSR_X1;

	// ---- 선으로 그리기 (1번 이미지 느낌) ----
	draw_line(xA0, yA0, xA1, yA1, CYAN);
	draw_line(xA1, yA1, xD1, yD1, CYAN);
	int lenS = xS1 - xD1;
	if (lenS > 0) {
		draw_line(xD1, yD1, xS1, yD1, CYAN);
	}
	draw_line(xS1, yS1, xR1, yR1, CYAN);

	// 라벨(선택)

}

static void draw_adsr_labels_static(void) {
	// ADSR 그래프 아래쪽에 위치
	int y = ADSR_LY0;

	// 크기 2로 개별 출력 (원하면 3도 가능)
	ILI9341_Draw_Text("A", 40, y, LIGHTGREY, 2, BLACK);
	ILI9341_Draw_Text("D", 90, y, LIGHTGREY, 2, BLACK);
	ILI9341_Draw_Text("S", 140, y, LIGHTGREY, 2, BLACK);
	ILI9341_Draw_Text("R", 190, y, LIGHTGREY, 2, BLACK);
}

static void draw_adsr_selection(void) {
	int x_pos[4] = { 40, 90, 140, 190 };

	// 라벨 영역만 지움(너무 크게 지우면 그래프까지 지울 수 있음)
	ILI9341_Draw_Filled_Rectangle_Coord(ADSR_X0, ADSR_LY0, ADSR_X1, ADSR_LY1,
			BLACK);

	// 라벨 다시 출력(여긴 4글자라 부담 거의 없음)
	draw_adsr_labels_static();

	int x = x_pos[g_adsr_sel];

	// 선택 박스
	ILI9341_Draw_Hollow_Rectangle_Coord(x - 6, ADSR_LY0, x + 18, ADSR_LY1,
			YELLOW);
}

static void draw_line(int x0, int y0, int x1, int y1, uint16_t color) {
	int dx = (x1 > x0) ? (x1 - x0) : (x0 - x1);
	int sx = (x0 < x1) ? 1 : -1;
	int dy = (y1 > y0) ? (y0 - y1) : (y1 - y0); // note: inverted
	int sy = (y0 < y1) ? 1 : -1;
	int err = dx + dy;

	while (1) {
		if (x0 >= 0 && x0 < LCD_W && y0 >= 0 && y0 < LCD_H) {
			ILI9341_Draw_Pixel((uint16_t) x0, (uint16_t) y0, color);
		}
		if (x0 == x1 && y0 == y1)
			break;
		int e2 = 2 * err;
		if (e2 >= dy) {
			err += dy;
			x0 += sx;
		}
		if (e2 <= dx) {
			err += dx;
			y0 += sy;
		}
	}
}

static void draw_note_center(void) {
	static char prev[8] = { 0 };

	// 노트 이름 테이블 (원하는 표기대로 바꿔도 됨)
	static const char *NAMES[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G",
			"G#", "A", "A#", "B" };

	char buf[8];
	uint8_t n = g_ui_note % 12;
	uint8_t o = g_ui_oct;

	// 예: "C#4"
	snprintf(buf, sizeof(buf), "%s%u", NAMES[n], (unsigned) o);

	// 이전과 같으면 안 그림
	if (strcmp(prev, buf) == 0)
		return;
	strcpy(prev, buf);

	// ✅ NOTE 영역만 클리어 (절대 다른 영역 침범 X)
	ILI9341_Draw_Filled_Rectangle_Coord(0, NOTE_Y0, LCD_W - 1, NOTE_Y1, BLACK);

	int x = 90;
	int y = NOTE_Y0 + 4;  // ✅ NOTE 영역 안
	ILI9341_Draw_Text(buf, x, y, WHITE, 4, BLACK);
}

static void draw_volume_bar(int top) {
	// 위치 선택: 위 or 아래
	(void) top; // 지금은 bottom 고정이면 top 무시해도 됨

	int x0 = VOL_X0;
	int x1 = VOL_X1;
	int y0 = VOL_Y0;
	int y1 = VOL_Y1;

	int w = (x1 - x0 + 1);

	ILI9341_Draw_Hollow_Rectangle_Coord(x0, y0, x1, y1, WHITE);

	uint8_t v = g_ui_vol;
	if (v > 100)
		v = 100;

	int fill = (w - 2) * v / 100;

	ILI9341_Draw_Filled_Rectangle_Coord(x0 + 1, y0 + 1, x1 - 1, y1 - 1, BLACK);

	if (fill > 0) {
		int xf = x0 + 1 + fill;
		if (xf > x1 - 1)
			xf = x1 - 1;
		ILI9341_Draw_Filled_Rectangle_Coord(x0 + 1, y0 + 1, xf, y1 - 1, GREEN);
	}
}

static void UI_SelectVolumeMode(void) {
	g_ui_edit_mode = UI_EDIT_VOLUME;
	g_ui_vol_dirty = 1;
	// (선택) 볼륨 모드 들어가면 다른 강조는 최신상태로 한번 정리
	g_ui_adsr_sel_dirty = 1;
	g_ui_filter_sel_dirty = 1;
}

static void draw_wave_graph(void) {
	int x0 = WAVE_X0;
	int x1 = WAVE_X1;
	int y0 = WAVE_Y0;
	int y1 = WAVE_Y1;

	int w = (x1 - x0 + 1);
	int h = (y1 - y0 + 1);

	// 프레임
	ILI9341_Draw_Hollow_Rectangle_Coord(x0, y0, x1, y1, WHITE);

	// 제목 (영역 밖이면 지워질 수 있으니 안전하게 y0 안으로 넣어도 됨)
	// ILI9341_Draw_Text("WAVE", 105, y0 - 15, WHITE, 1, BLACK);

	// 중앙 기준선
	int midY = y0 + h / 2;
	for (int x = x0 + 1; x < x1; x += 4) {
		ILI9341_Draw_Pixel(x, midY, DARKGREY);
	}

	// 표시용 진폭 (픽셀)
	int ampPix = (h * 35) / 100;   // h*0.35

	int prevX = x0 + 1;
	int prevY = midY;

	// 2주기: phase가 0~2047 범위를 한 화면에 매핑
	for (int i = 0; i < (w - 2); i++) {
		int x = x0 + 1 + i;

		// phase: 0~2047 (2주기)
		// 1024 LUT를 쓰기 위해 0~1023로 만들면 됨(2주기니까 *2)
		int phase2 = (i * 2048) / (w - 2);   // 0..2047
		int idx = phase2 & 1023;             // 0..1023 (LUT 인덱스)

		int y = midY;

		if (g_ui_wave == UI_WAVE_SINE) {
			// sin_samples: 대략 120 중심, 진폭 50
			int s = (int) sin_samples[idx] - 120;      // 대략 -50..+50
			y = midY - (s * ampPix) / 50;
		} else if (g_ui_wave == UI_WAVE_SQUARE) {
			int s = (int) sin_samples[idx] - 120;
			y = midY - ((s >= 0) ? ampPix : -ampPix);
		} else { // UI_WAVE_SAW 를 삼각파로 사용
				 // 삼각파: phase(0..1023) -> -amp..+amp
				 // 0..511: -1 -> +1, 512..1023: +1 -> -1
			int p = idx; // 0..1023
			int tri;
			if (p < 512) {
				tri = -ampPix + (p * (2 * ampPix)) / 511;
			} else {
				tri = +ampPix - ((p - 512) * (2 * ampPix)) / 511;
			}
			y = midY - tri;
		}

		// 화면 밖 보호(드라이버에 따라 안전장치)
		if (y < y0 + 1)
			y = y0 + 1;
		if (y > y1 - 1)
			y = y1 - 1;

		if (i > 0) {
			draw_line(prevX, prevY, x, y, RED);
		}
		prevX = x;
		prevY = y;
	}
}

static void draw_filter_labels_static(void) {
	int y = FILTER_LY0 + 2;  // ✅ 라벨 영역 기준

	// 글씨 크기 2 (조금만 키움)
	ILI9341_Draw_Text("CUTOFF", 32, y, LIGHTGREY, 2, BLACK);
	ILI9341_Draw_Text("RESONANCE", 130, y, LIGHTGREY, 2, BLACK);
}

static void draw_filter_selection(void) {
	int y = FILTER_LY0 + 2;

	// 라벨 영역만 클리어 (라벨 줄 + 밑줄 줄 포함)
	// 글씨 크기 2면 높이가 대략 16px 정도라서 여유로 22~26 잡자
	ILI9341_Draw_Filled_Rectangle_Coord(0, FILTER_LY0, LCD_W - 1, FILTER_LY1,
			BLACK);

	// 기본 라벨(회색)
	draw_filter_labels_static();

	// 선택 강조: 글씨 색 변경 + 밑줄
	if (g_filter_sel == FILTER_SEL_CUTOFF) {
		// 글씨 강조
		ILI9341_Draw_Text("CUTOFF", 32, y, WHITE, 2, BLACK);
		// 밑줄 (텍스트 폭에 맞춰 적당히)
		// "CUTOFF" (size=2) 대충 6글자 * (6px*2)=72px 정도라서 72~80px 정도 추천
		ILI9341_Draw_Horizontal_Line(30, y + 20, 80, YELLOW);
	} else {
		ILI9341_Draw_Text("RESONANCE", 130, y, WHITE, 2, BLACK);
		// "RESONANCE" 9글자 → 대충 9*(6*2)=108px 정도
		ILI9341_Draw_Horizontal_Line(128, y + 20, 110, YELLOW);
	}
}

// ================== [추가 2] 값/선택 처리 로직 ==================
static int clampi(int v, int lo, int hi) {
	if (v < lo)
		return lo;
	if (v > hi)
		return hi;
	return v;
}

// ADSR 선택(A->D->S->R->A...) 이동
static void UI_MoveAdsrSelect(void) {
	g_ui_edit_mode = UI_EDIT_ADSR;

	g_adsr_sel = (UI_ADSR_Select_t) ((g_adsr_sel + 1) % 4);
	g_ui_adsr_sel_dirty = 1;   // ✅ 라벨 강조만 다시 그리기
}

// FILTER 선택(CUTOFF <-> RESONANCE) 토글
static void UI_ToggleFilterSelect(void) {
	g_ui_edit_mode = UI_EDIT_FILTER;

	g_filter_sel =
			(g_filter_sel == FILTER_SEL_CUTOFF) ?
					FILTER_SEL_RESO : FILTER_SEL_CUTOFF;
	g_ui_filter_sel_dirty = 1; // ✅ 라벨 강조만 다시 그리기
}

// 엔코더 회전 델타(+1/-1)에 따라 현재 선택된 값을 변경
void UI_OnEncoderDelta(int delta) {
	if (delta == 0)
		return;

	if (g_ui_edit_mode == UI_EDIT_ADSR) {
		switch (g_adsr_sel) {
		case ADSR_SEL_A:
			g_ui_adsr.attack_steps = (uint32_t) clampi(
					(int) g_ui_adsr.attack_steps + delta, 1, 200);
			break;
		case ADSR_SEL_D:
			g_ui_adsr.decay_steps = (uint32_t) clampi(
					(int) g_ui_adsr.decay_steps + delta, 1, 200);
			break;
		case ADSR_SEL_S:
			g_ui_adsr.sustain_level = (uint32_t) clampi(
					(int) g_ui_adsr.sustain_level + delta, 0, 100);
			break;
		case ADSR_SEL_R:
			g_ui_adsr.release_steps = (uint32_t) clampi(
					(int) g_ui_adsr.release_steps + delta, 1, 200);
			break;
		}
		g_ui_adsr_dirty = 1; // ✅ ADSR 그래프만 갱신
	} else if (g_ui_edit_mode == UI_EDIT_FILTER) {
		// ✅ 이게 바로 ... 이었던 부분
		if (g_filter_sel == FILTER_SEL_CUTOFF) {
			g_ui_cutoff = (uint8_t) clampi((int) g_ui_cutoff + delta, 0, 100);
		} else {
			g_ui_reso = (uint8_t) clampi((int) g_ui_reso + delta, 0, 100);
		}
		g_ui_filter_dirty = 1;
	} else { // UI_EDIT_VOLUME
		int v = (int) g_ui_vol + delta;
		v = clampi(v, 0, 100);
		g_ui_vol = (uint8_t) v;
		g_ui_vol_dirty = 1;
	}
}
// ================================================================

static void LCD_Task(void *argument) {
	uint32_t received;
	uint8_t screen_mode = LCD_STATE_GRAPH_VIEW;
	uint8_t last_screen_mode = 255;

	for (;;) {

		if (xQueueReceive(lcdQueueHandle, &received, 0) == pdTRUE) {
			screen_mode = (uint8_t) received;

			if (screen_mode == LCD_STATE_GRAPH_VIEW) {
				g_ui_dirty = 1;
			} else {
				ILI9341_Fill_Screen(BLACK);
			}
		}

		if (screen_mode == LCD_STATE_GRAPH_VIEW) {

			// 1) 화면 전체를 다시 그려야 할 때(처음 진입 / 큰 변경)
			if (g_ui_dirty) {
				g_ui_dirty = 0;

				ILI9341_Fill_Screen(BLACK);
				draw_adsr_graph();
				draw_wave_graph();

				draw_adsr_labels_static();
				draw_adsr_selection();

				draw_filter_selection();

				// ✅ (추가) 그래프 화면 첫 진입 시에도 한 번 표시
				g_ui_note_dirty = 1;
				g_ui_vol_dirty = 1;

				g_ui_adsr_dirty = 0;
				g_ui_filter_dirty = 0;

				g_ui_adsr_sel_dirty = 0;
				g_ui_filter_sel_dirty = 0;
			}

			// 2) 부분 업데이트(ADSR/FILTER 그래프)
			if (g_ui_adsr_dirty) {
				g_ui_adsr_dirty = 0;

				// ADSR 그래프 영역만 지우고 다시 그림
				ILI9341_Draw_Filled_Rectangle_Coord(ADSR_X0, ADSR_Y0, ADSR_X1,
						ADSR_Y1, BLACK);
				draw_adsr_graph();
			}

			if (g_ui_filter_dirty) {
				g_ui_filter_dirty = 0;

				// FILTER 그래프 영역만 지우고 다시 그림
				ILI9341_Draw_Filled_Rectangle_Coord(WAVE_X0, WAVE_Y0, WAVE_X1,
						WAVE_Y1, BLACK);
				draw_wave_graph();
			}

			// ================== [추가 3] 선택(강조 박스)만 갱신 ==================
			if (g_ui_adsr_sel_dirty) {
				g_ui_adsr_sel_dirty = 0;
				draw_adsr_selection();
			}

			if (g_ui_filter_sel_dirty) {
				g_ui_filter_sel_dirty = 0;
				draw_filter_selection();
			}
			// ================================================================

			// 2) 부분 업데이트(노트/볼륨만 바뀌었을 때)
			if (g_ui_note_dirty) {
				g_ui_note_dirty = 0;
				draw_note_center();
			}

			if (g_ui_vol_dirty) {
				g_ui_vol_dirty = 0;
				draw_volume_bar(0); // 0=아래, 1=위
			}
		} else if (screen_mode != last_screen_mode) {
			if (screen_mode == LCD_STATE_MAIN_DASH)
				draw_main_dashboard();
			last_screen_mode = screen_mode;
		}

		vTaskDelay(pdMS_TO_TICKS(30));
	}

}

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_PIN) {
	static uint32_t last_tick_ui = 0;   // ✅ 나머지(ADSR/FILTER/VOL/ENC) 전용

	// ================== 2) 나머지 버튼/엔코더는 별도 디바운스 ==================
	if (HAL_GetTick() - last_tick_ui < 50)
		return;
	last_tick_ui = HAL_GetTick();

	if (GPIO_PIN == Rotary1_KEY_Pin) {
		UI_MoveAdsrSelect();
//		UI_ToggleFilterSelect(); // 잘 안됨
//		UI_OnEncoderDelta(+1);
//		UI_OnEncoderDelta(-1);

//		g_ui_wave = (UI_Wave_t)((g_ui_wave + 1) % 3); // SINE->SQUARE->SAW(=TRI) 순환
		//        g_ui_filter_dirty = 1; // 아래 그래프 다시 그림
		return;
	}
}
