#include "ui.h"
#include "main.h"
#include "ILI9341_STM32_Driver.h"
#include "ILI9341_GFX.h"
#include <string.h>
#include <math.h>
#include "task.h"
#include "user_rtos.h"
#include <stdio.h>

TaskHandle_t lcdTaskHandle = NULL;
QueueHandle_t lcdQueueHandle = NULL;
LcdState_t currentLcdState = LCD_STATE_GRAPH_VIEW;

#define LCD_W   240
#define LCD_H   320

//uint8_t sin_samples[1024];

// ===== Dirty Flags (구조체로 통합) =====
volatile UI_DirtyFlags_t g_ui_dirty = { .full_redraw = 1, .adsr_graph = 0,
		.adsr_sel = 0, .wave_graph = 0, .filter_sel = 0, .note_display = 0,
		.volume_bar = 0 };

// ===== UI 상태 변수 =====
volatile UI_ADSR_t g_ui_adsr = { .attack_steps = 40, .decay_steps = 30,
		.sustain_level = 50, .release_steps = 60 };

volatile uint8_t g_ui_wave_ready = 0;

volatile UI_Wave_t g_ui_wave = UI_WAVE_SINE;
volatile UI_EditMode_t g_ui_edit_mode = UI_EDIT_ADSR;
volatile UI_ADSR_Select_t g_adsr_sel = ADSR_SEL_A;
volatile UI_Filter_Select_t g_filter_sel = FILTER_SEL_CUTOFF;

volatile uint8_t g_ui_note = 0;
volatile uint8_t g_ui_oct = 4;
volatile uint8_t g_ui_vol = 80;
volatile uint8_t g_ui_cutoff = 50;
volatile uint8_t g_ui_reso = 30;

// ===== Layout constants =====
#define UI_M        6

// ADSR graph (top)
#define ADSR_X0     10
#define ADSR_W      220
#define ADSR_Y0     UI_M
#define ADSR_H      108
#define ADSR_X1     (ADSR_X0 + ADSR_W - 1)
#define ADSR_Y1     (ADSR_Y0 + ADSR_H - 1)

// ADSR label row
#define ADSR_LH     20
#define ADSR_LY0    (ADSR_Y1 + 4)
#define ADSR_LY1    (ADSR_LY0 + ADSR_LH - 1)

// Note area
#define NOTE_H      40
#define NOTE_Y0     (ADSR_LY1 + 6)
#define NOTE_Y1     (NOTE_Y0 + NOTE_H - 1)

// Filter label row
#define FILTER_LH   22
#define FILTER_LY0  (NOTE_Y1 + 6)
#define FILTER_LY1  (FILTER_LY0 + FILTER_LH - 1)

// Wave graph
#define WAVE_X0     10
#define WAVE_W      220
#define WAVE_H      90
#define WAVE_X1     (WAVE_X0 + WAVE_W - 1)
#define WAVE_Y0     (FILTER_LY1 + 6)
#define WAVE_Y1     (WAVE_Y0 + WAVE_H - 1)

// Volume bar (bottom)
#define VOL_X0      10
#define VOL_W       220
#define VOL_H       8
#define VOL_X1      (VOL_X0 + VOL_W - 1)
#define VOL_Y0      (LCD_H - UI_M - VOL_H)
#define VOL_Y1      (VOL_Y0 + VOL_H - 1)

// ===== 함수 선언 =====
static void LCD_Task(void *argument);
//static void Generate_Sine_Samples(void);
static void draw_main_dashboard(void);
static void draw_line(int x0, int y0, int x1, int y1, uint16_t color);

static void draw_adsr_graph(void);
static void draw_adsr_labels_static(void);
static void draw_adsr_selection(void);

static void draw_wave_graph(void);

static void draw_filter_labels_static(void);
static void draw_filter_selection(void);

static void draw_note_center(void);
static void draw_volume_bar(void);

static void UI_MoveAdsrSelect(void);
static void UI_ToggleFilterSelect(void);
static void UI_SelectVolumeMode(void);
static int clampi(int v, int lo, int hi);

// ===== 초기화 =====
void display_init(void) {
//	Generate_Sine_Samples();

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
//
//static void Generate_Sine_Samples(void) {
//	for (int i = 0; i < 1024; i++) {
//		sin_samples[i] =
//				(uint8_t) (120 + 50 * sin(5.0 * 2 * M_PI * i / 1024.0));
//	}
//}

// ===== 메인 대시보드 =====
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

// ===== ADSR 그래프 =====
static void draw_adsr_graph(void) {
	int x0 = ADSR_X0, y0 = ADSR_Y0, w = ADSR_W, h = ADSR_H;

	// 프레임
	ILI9341_Draw_Hollow_Rectangle_Coord(ADSR_X0, ADSR_Y0, ADSR_X1, ADSR_Y1,
	WHITE);

	// ADSR 파라미터를 화면 폭으로 매핑
	int wS = w / 4;

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

	int sum = wA + wD + wS + wR;
	if (sum != w)
		wR += (w - sum);

	// Y 좌표 계산
	int y_base = ADSR_Y1;
	int y_peak = ADSR_Y0;
	int h_pix = ADSR_H - 1;
	int y_sus = ADSR_Y0 + ((100 - g_ui_adsr.sustain_level) * h_pix) / 100;

	if (y_sus < ADSR_Y0)
		y_sus = ADSR_Y0;
	if (y_sus > ADSR_Y1)
		y_sus = ADSR_Y1;

	// ADSR 꼭짓점
	int xA0 = x0, yA0 = y_base;
	int xA1 = x0 + wA, yA1 = y_peak;
	int xD1 = xA1 + wD, yD1 = y_sus;
	int xS1 = xD1 + wS, yS1 = y_sus;
	int xR1 = xS1 + wR, yR1 = y_base;

	if (xR1 > ADSR_X1)
		xR1 = ADSR_X1;
	if (xA1 > ADSR_X1)
		xA1 = ADSR_X1;
	if (xD1 > ADSR_X1)
		xD1 = ADSR_X1;
	if (xS1 > ADSR_X1)
		xS1 = ADSR_X1;

	// 선으로 그리기
	draw_line(xA0, yA0, xA1, yA1, CYAN);
	draw_line(xA1, yA1, xD1, yD1, CYAN);
	int lenS = xS1 - xD1;
	if (lenS > 0) {
		draw_line(xD1, yD1, xS1, yD1, CYAN);
	}
	draw_line(xS1, yS1, xR1, yR1, CYAN);
}

static void draw_adsr_labels_static(void) {
	int y = ADSR_LY0;
	ILI9341_Draw_Text("A", 40, y, LIGHTGREY, 2, BLACK);
	ILI9341_Draw_Text("D", 90, y, LIGHTGREY, 2, BLACK);
	ILI9341_Draw_Text("S", 140, y, LIGHTGREY, 2, BLACK);
	ILI9341_Draw_Text("R", 190, y, LIGHTGREY, 2, BLACK);
}

static void draw_adsr_selection(void) {
	int x_pos[4] = { 40, 90, 140, 190 };

	// 라벨 영역만 지우기
	ILI9341_Draw_Filled_Rectangle_Coord(ADSR_X0, ADSR_LY0, ADSR_X1, ADSR_LY1,
	BLACK);

	// 라벨 다시 출력
	draw_adsr_labels_static();

	int x = x_pos[g_adsr_sel];

	// 선택 박스
	ILI9341_Draw_Hollow_Rectangle_Coord(x - 6, ADSR_LY0, x + 18, ADSR_LY1,
	YELLOW);
}

// ===== 파형 그래프 =====
static void draw_wave_graph(void) {
	int x0 = WAVE_X0;
	int x1 = WAVE_X1;
	int y0 = WAVE_Y0;
	int y1 = WAVE_Y1;

	int w = (x1 - x0 + 1); // 그래프 그릴 영역의 폭
	int h = (y1 - y0 + 1); // 높이
	int midY = y0 + (h / 2);

	int start_idx = 0;

	// 버퍼의 절반 정도까지만 검색 (안전하게)
	for (int i = 1; i < VIS_BUF_SIZE / 2; i++) {
		// 이전 값은 음수이고, 현재 값은 양수일 때 (상승 엣지)
		if (g_vis_buffer[i - 1] < 0 && g_vis_buffer[i] >= 0) {
			start_idx = i;
			break; // 찾았으면 중단
		}
	}

	// 1. 프레임 및 기준선 그리기
	ILI9341_Draw_Hollow_Rectangle_Coord(x0, y0, x1, y1, WHITE);

	// 중앙선 (점선)
	for (int x = x0 + 1; x < x1; x += 4) {
		ILI9341_Draw_Pixel(x, midY, DARKGREY);
	}

	// 2. 실제 파형 그리기
	int prevX = x0 + 1;
	int first_sample = g_vis_buffer[start_idx];
	int prevY = midY - (first_sample / 800);

	// 루프: 화면 폭(w) 만큼 돌거나, 버퍼 데이터 끝까지
	for (int i = 0; i < (w - 2); i++) {
		if (i >= VIS_BUF_SIZE)
			break; // 버퍼 오버플로우 방지

		int data_idx = start_idx + i;

		if (data_idx >= VIS_BUF_SIZE)
			break; // 버퍼 끝 체크

		int x = x0 + 1 + i;
		int16_t sample = g_vis_buffer[data_idx];

		int offset_y = -(sample / 64);

		if (offset_y > 40)
			offset_y = 40;
		if (offset_y < -40)
			offset_y = -40;

		int y = midY + offset_y;

		// 클리핑 (화면 밖으로 나가는 것 방지)
		if (y < y0 + 1)
			y = y0 + 1;
		if (y > y1 - 1)
			y = y1 - 1;

		// 선 이어 그리기
		if (i > 0) {
			draw_line(prevX, prevY, x, y, CYAN); // 색상을 CYAN 등으로 변경 추천
		}

		prevX = x;
		prevY = y;
	}
}

// ===== 필터 라벨 =====
static void draw_filter_labels_static(void) {
	int y = FILTER_LY0 + 2;
	ILI9341_Draw_Text("CUTOFF", 32, y, LIGHTGREY, 2, BLACK);
	ILI9341_Draw_Text("RESONANCE", 130, y, LIGHTGREY, 2, BLACK);
}

static void draw_filter_selection(void) {
	int y = FILTER_LY0 + 2;

	// 라벨 영역 클리어
	ILI9341_Draw_Filled_Rectangle_Coord(0, FILTER_LY0, LCD_W - 1, FILTER_LY1,
	BLACK);

	// 기본 라벨
	draw_filter_labels_static();

	// 선택 강조
	if (g_filter_sel == FILTER_SEL_CUTOFF) {
		ILI9341_Draw_Text("CUTOFF", 32, y, WHITE, 2, BLACK);
		ILI9341_Draw_Horizontal_Line(30, y + 20, 80, YELLOW);
	} else {
		ILI9341_Draw_Text("RESONANCE", 130, y, WHITE, 2, BLACK);
		ILI9341_Draw_Horizontal_Line(128, y + 20, 110, YELLOW);
	}
}

// ===== 음계 표시 =====
static void draw_note_center(void) {
	static char prev[8] = { 0 };
	static const char *NAMES[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G",
			"G#", "A", "A#", "B" };

	char buf[8];
	uint8_t n = g_ui_note % 12;
	uint8_t o = g_ui_oct;

	snprintf(buf, sizeof(buf), "%s%u", NAMES[n], (unsigned) o);

	if (strcmp(prev, buf) == 0)
		return;
	strcpy(prev, buf);

	// NOTE 영역만 클리어
	ILI9341_Draw_Filled_Rectangle_Coord(0, NOTE_Y0, LCD_W - 1, NOTE_Y1, BLACK);

	int x = 90;
	int y = NOTE_Y0 + 4;
	ILI9341_Draw_Text(buf, x, y, WHITE, 4, BLACK);
}

// ===== 볼륨 바 =====
static void draw_volume_bar(void) {
	int x0 = VOL_X0, x1 = VOL_X1, y0 = VOL_Y0, y1 = VOL_Y1;
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

// ===== 선 그리기 =====
static void draw_line(int x0, int y0, int x1, int y1, uint16_t color) {
	int dx = (x1 > x0) ? (x1 - x0) : (x0 - x1);
	int sx = (x0 < x1) ? 1 : -1;
	int dy = (y1 > y0) ? (y0 - y1) : (y1 - y0);
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

// ===== 선택 처리 로직 =====
static int clampi(int v, int lo, int hi) {
	if (v < lo)
		return lo;
	if (v > hi)
		return hi;
	return v;
}

static void UI_MoveAdsrSelect(void) {
	g_ui_edit_mode = UI_EDIT_ADSR;
	g_adsr_sel = (UI_ADSR_Select_t) ((g_adsr_sel + 1) % 4);
	g_ui_dirty.adsr_sel = 1;  // ✅ 구조체 사용
}

static void UI_ToggleFilterSelect(void) {
	g_ui_edit_mode = UI_EDIT_FILTER;
	g_filter_sel =
			(g_filter_sel == FILTER_SEL_CUTOFF) ?
					FILTER_SEL_RESO : FILTER_SEL_CUTOFF;
	g_ui_dirty.filter_sel = 1;  // ✅ 구조체 사용
}

static void UI_SelectVolumeMode(void) {
	g_ui_edit_mode = UI_EDIT_VOLUME;
	g_ui_dirty.volume_bar = 1;
	g_ui_dirty.adsr_sel = 1;
	g_ui_dirty.filter_sel = 1;
}

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
		g_ui_dirty.adsr_graph = 1;  // ✅ 구조체 사용
	} else if (g_ui_edit_mode == UI_EDIT_FILTER) {
		if (g_filter_sel == FILTER_SEL_CUTOFF) {
			g_ui_cutoff = (uint8_t) clampi((int) g_ui_cutoff + delta, 0, 100);
		} else {
			g_ui_reso = (uint8_t) clampi((int) g_ui_reso + delta, 0, 100);
		}
		g_ui_dirty.wave_graph = 1;  // ✅ 구조체 사용
	} else {
		int v = (int) g_ui_vol + delta;
		v = clampi(v, 0, 100);
		g_ui_vol = (uint8_t) v;
		g_ui_dirty.volume_bar = 1;  // ✅ 구조체 사용
	}
}

// 1. 볼륨 변경 함수 (Rotary 2에서 호출)
void UI_OnChangeVolume(int delta) {
	int new_vol = (int) g_ui_vol + delta;

	// 0~100 제한
	if (new_vol > 100)
		new_vol = 100;
	if (new_vol < 0)
		new_vol = 0;

	g_ui_vol = (uint8_t) new_vol;
	g_ui_dirty.volume_bar = 1; // 화면(볼륨 바) 갱신 요청
}

// 2. 옥타브 변경 함수 (나중에 버튼에서 호출)
void UI_OnChangeOctave(int delta) {
	int new_oct = (int) g_ui_oct + delta;

	// 1~8 옥타브 제한
	if (new_oct > 8)
		new_oct = 8;
	if (new_oct < 1)
		new_oct = 1;

	g_ui_oct = (uint8_t) new_oct;
	g_ui_dirty.note_display = 1; // 화면(중앙 노트/옥타브 표시) 갱신 요청
}

// ===== LCD Task =====
static void LCD_Task(void *argument) {
	uint32_t received;
	uint8_t screen_mode = LCD_STATE_GRAPH_VIEW;
	uint8_t last_screen_mode = 255;

	for (;;) {
		if (xQueueReceive(lcdQueueHandle, &received, 0) == pdTRUE) {
			screen_mode = (uint8_t) received;

			if (screen_mode == LCD_STATE_GRAPH_VIEW) {
				g_ui_dirty.full_redraw = 1;  // ✅ 구조체 사용
			} else {
				ILI9341_Fill_Screen(BLACK);
			}
		}

		if (screen_mode == LCD_STATE_GRAPH_VIEW) {
			// 전체 다시 그리기
			if (g_ui_dirty.full_redraw) {
				g_ui_dirty.full_redraw = 0;

				ILI9341_Fill_Screen(BLACK);
				draw_adsr_graph();
				draw_wave_graph();
				draw_adsr_labels_static();
				draw_adsr_selection();
				draw_filter_selection();

				// 나머지 플래그도 초기화
				g_ui_dirty.adsr_graph = 0;
				g_ui_dirty.adsr_sel = 0;
				g_ui_dirty.wave_graph = 0;
				g_ui_dirty.filter_sel = 0;
				g_ui_dirty.note_display = 1;  // 첫 진입 시 표시
				g_ui_dirty.volume_bar = 1;    // 첫 진입 시 표시
			}

			// 부분 업데이트 (ADSR 그래프)
			if (g_ui_dirty.adsr_graph) {
				g_ui_dirty.adsr_graph = 0;
				ILI9341_Draw_Filled_Rectangle_Coord(ADSR_X0, ADSR_Y0, ADSR_X1,
				ADSR_Y1, BLACK);
				draw_adsr_graph();
			}

			// 부분 업데이트 (ADSR 선택)
			if (g_ui_dirty.adsr_sel) {
				g_ui_dirty.adsr_sel = 0;
				draw_adsr_selection();
			}

			// 부분 업데이트 (파형 그래프)
			if (g_ui_dirty.wave_graph) {
				g_ui_dirty.wave_graph = 0;
				ILI9341_Draw_Filled_Rectangle_Coord(WAVE_X0, WAVE_Y0, WAVE_X1,
				WAVE_Y1, BLACK);
				draw_wave_graph();
			}

			// 부분 업데이트 (필터 선택)
			if (g_ui_dirty.filter_sel) {
				g_ui_dirty.filter_sel = 0;
				draw_filter_selection();
			}

			// 부분 업데이트 (음계)
			if (g_ui_dirty.note_display) {
				g_ui_dirty.note_display = 0;
				draw_note_center();
			}

			// 부분 업데이트 (볼륨)
			if (g_ui_dirty.volume_bar) {
				g_ui_dirty.volume_bar = 0;
				draw_volume_bar();
			}
		} else if (screen_mode != last_screen_mode) {
			if (screen_mode == LCD_STATE_MAIN_DASH)
				draw_main_dashboard();
			last_screen_mode = screen_mode;
		}

		vTaskDelay(pdMS_TO_TICKS(30));
	}
}

// ===== 인터럽트 콜백 =====
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_PIN) {
	static uint32_t last_tick_ui = 0;

	if (HAL_GetTick() - last_tick_ui < 50)
		return;
	last_tick_ui = HAL_GetTick();

	if (GPIO_PIN == Rotary1_KEY_Pin) {
		UI_MoveAdsrSelect();
		return;
	}
//
//	if (GPIO_PIN == Rotary2_KEY_Pin) {
//		return; // 화면 전환 막고 옥타브만 테스트하려면 return
//	}
}
