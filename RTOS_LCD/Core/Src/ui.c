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

const char* wave_names[] = {"SINE", "SQUARE", "SAW"};

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
// 각각 사용할 좌표를 미리 정의
#define TITLE_Y0    0
#define TITLE_Y1    30

#define ADSR_X0     10
#define ADSR_Y0     40
#define ADSR_Y1     120

#define ADSR_LABEL_Y    135

#define WAVE_TITLE_Y0   160
#define WAVE_TITLE_Y1   185

#define FILTER_Y        195

#define VOL_Y           220

#define WAVE_GRAPH_Y0   240
#define WAVE_GRAPH_Y1   310

#define LCD_W   240
#define LCD_H   320

// ===== 함수 선언 =====
static void LCD_Task(void *argument);
//static void Generate_Sine_Samples(void);
static void draw_main_dashboard(void);
static void draw_title_bar(void);
static void draw_line(int x0, int y0, int x1, int y1, uint16_t color);

static void draw_adsr_graph(void);
static void draw_adsr_labels(void);

static void draw_wave_title(void);
static void draw_wave_graph(void);

static void draw_filter_labels(void);

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

static void draw_title_bar(void) {
    ILI9341_Draw_Filled_Rectangle_Coord(0, TITLE_Y0, 240, TITLE_Y1, CYAN);
    ILI9341_Draw_Text("ADSR ENVELOPE", 42, 8, BLACK, 2, CYAN);
    ILI9341_Draw_Horizontal_Line(0, TITLE_Y1, 240, WHITE);
}

// ===== ADSR 그래프 =====
// ===== ADSR 그래프 =====
static void draw_adsr_graph(void) {
	int x0 = 10;
	int w = 220;
	int h = (ADSR_Y1 - ADSR_Y0 + 1);

	// 프레임
	ILI9341_Draw_Hollow_Rectangle_Coord(10, ADSR_Y0, 230, ADSR_Y1, WHITE);

	// ADSR 파라미터를 화면 폭으로 매핑
	int wS = w / 4;

	uint32_t A = g_ui_adsr.attack_steps;
	uint32_t D = g_ui_adsr.decay_steps;
	uint32_t R = g_ui_adsr.release_steps;

	uint32_t denom = (A + D + R);
	if (denom == 0) denom = 1;

	int avail = (w - wS);
	int wA = (int) ((uint64_t) avail * A / denom);
	int wD = (int) ((uint64_t) avail * D / denom);
	int wR = (int) ((uint64_t) avail * R / denom);

	int sum = wA + wD + wS + wR;
	if (sum != w) wR += (w - sum);

	// Y 좌표 계산
	int y_base = ADSR_Y1;
	int y_peak = ADSR_Y0;
	int h_pix = h - 1;
	int y_sus = ADSR_Y0 + ((100 - g_ui_adsr.sustain_level) * h_pix) / 100;

	if (y_sus < ADSR_Y0) y_sus = ADSR_Y0;
	if (y_sus > ADSR_Y1) y_sus = ADSR_Y1;

	// ADSR 꼭짓점
	int xA0 = x0, yA0 = y_base;
	int xA1 = x0 + wA, yA1 = y_peak;
	int xD1 = xA1 + wD, yD1 = y_sus;
	int xS1 = xD1 + wS, yS1 = y_sus;
	int xR1 = xS1 + wR, yR1 = y_base;

	if (xR1 > 230) xR1 = 230;
	if (xA1 > 230) xA1 = 230;
	if (xD1 > 230) xD1 = 230;
	if (xS1 > 230) xS1 = 230;

	// 선으로 그리기
	draw_line(xA0, yA0, xA1, yA1, YELLOW);
	draw_line(xA1, yA1, xD1, yD1, YELLOW);
	int lenS = xS1 - xD1;
	if (lenS > 0) {
		draw_line(xD1, yD1, xS1, yD1, YELLOW);
	}
	draw_line(xS1, yS1, xR1, yR1, YELLOW);
}

static void draw_adsr_labels(void) {
	// 라벨 영역만 지우기
	ILI9341_Draw_Filled_Rectangle_Coord(0, ADSR_LABEL_Y, 240, ADSR_LABEL_Y + 20, BLACK);

	// ADSR 모드일 때만 선택 색상 적용
	uint16_t color_A = WHITE;
	uint16_t color_D = WHITE;
	uint16_t color_S = WHITE;
	uint16_t color_R = WHITE;

	if (g_ui_edit_mode == UI_EDIT_ADSR) {  // ← 이 조건 추가!
		if (g_adsr_sel == ADSR_SEL_A) color_A = RED;
		else if (g_adsr_sel == ADSR_SEL_D) color_D = RED;
	    else if (g_adsr_sel == ADSR_SEL_S) color_S = RED;
	    else if (g_adsr_sel == ADSR_SEL_R) color_R = RED;
	}

	ILI9341_Draw_Text("A", 40, ADSR_LABEL_Y, color_A, 2, BLACK);
	ILI9341_Draw_Text("D", 90, ADSR_LABEL_Y, color_D, 2, BLACK);
	ILI9341_Draw_Text("S", 140, ADSR_LABEL_Y, color_S, 2, BLACK);
	ILI9341_Draw_Text("R", 190, ADSR_LABEL_Y, color_R, 2, BLACK);
}

static void draw_wave_title(void) {
    // 선택 여부에 따라 색상 변경
	uint16_t bar_color = (g_ui_edit_mode == UI_EDIT_FILTER) ? RED : MAGENTA;

	// 타이틀 바 영역만 지우고 다시 그리기
	ILI9341_Draw_Filled_Rectangle_Coord(0, WAVE_TITLE_Y0, 240, WAVE_TITLE_Y1, bar_color);

	// 음계만 중앙에 표시 (파형 이름 제거)
	static const char *notes_list[] = {"C", "C#", "D", "D#", "E", "F",
	                                        "F#", "G", "G#", "A", "A#", "B"};
	char note_buf[12];
	sprintf(note_buf, "NOTE:%s%d", notes_list[g_ui_note % 12], g_ui_oct);
	ILI9341_Draw_Text(note_buf, 55, 161, YELLOW, 3, bar_color);  // ← X좌표 70으로 중앙 배치
}

// ===== 파형 그래프 =====
static void draw_wave_graph(void) {
	int x0 = 10;
	int x1 = 230;
	int y0 = WAVE_GRAPH_Y0;
	int y1 = WAVE_GRAPH_Y1;

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
			draw_line(prevX, prevY, x, y, GREEN);; // 색상을 CYAN 등으로 변경 추천
		}

		prevX = x;
		prevY = y;
	}
}

// ===== 필터 라벨 =====

static void draw_filter_labels(void) {
	// 필터 라벨 영역만 지우기
	    ILI9341_Draw_Filled_Rectangle_Coord(0, FILTER_Y, 240, FILTER_Y + 20, BLACK);

	    // 현재 편집 모드가 필터일 때만 색상 변경
	    uint16_t color_cutoff = WHITE;
	    uint16_t color_reso = WHITE;

	    if (g_ui_edit_mode == UI_EDIT_FILTER) {
	        if (g_filter_sel == FILTER_SEL_CUTOFF) {
	            color_cutoff = RED;
	        } else {
	            color_reso = RED;
	        }
	    }

	    ILI9341_Draw_Text("CUTOFF", 45, FILTER_Y, color_cutoff, 2, BLACK);
	    ILI9341_Draw_Text("RESO", 145, FILTER_Y, color_reso, 2, BLACK);
}


// ===== 볼륨 바 =====
static void draw_volume_bar(void) {
	// 볼륨 영역만 지우기
	    ILI9341_Draw_Filled_Rectangle_Coord(0, VOL_Y, 240, VOL_Y + 15, BLACK);

	    // 현재 편집 모드가 볼륨일 때만 RED로 표시
	    uint16_t vol_color = (g_ui_edit_mode == UI_EDIT_VOLUME) ? RED : WHITE;

	    // "Vol" 텍스트
	    ILI9341_Draw_Text("Vol", 1, VOL_Y-5, vol_color, 2, BLACK);

	    // 10칸 막대 게이지
	    for(int i = 0; i < 10; i++) {
	    	uint16_t bar_x = 39 + (i * 17);
	        if (i < (g_ui_vol / 10)) {
	            // 채워진 칸
	            ILI9341_Draw_Filled_Rectangle_Coord(bar_x, VOL_Y, bar_x + 13, VOL_Y + 8, vol_color);
	        } else {
	            // 빈 칸 (테두리만)
	            ILI9341_Draw_Rectangle(bar_x, VOL_Y, 13, 8, DARKGREY);
	        }
	    }

	    // 숫자 표시
	    char vol_buf[6];
	    sprintf(vol_buf, "%d  ", g_ui_vol);
	    ILI9341_Draw_Text(vol_buf, 210, VOL_Y-5, vol_color, 2, BLACK);
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
	// 1. 현재 ADSR 모드인 경우
	if (g_ui_edit_mode == UI_EDIT_ADSR) {
		// R(마지막)이 아니면 다음 칸(D, S, R)으로 이동
		if (g_adsr_sel < ADSR_SEL_R) {
			g_adsr_sel = (UI_ADSR_Select_t)(g_adsr_sel + 1);
		}
		// R에서 한 번 더 누르면 -> 필터 모드(Cutoff)로 진입!
		else {
			g_ui_edit_mode = UI_EDIT_FILTER;
			g_filter_sel = FILTER_SEL_CUTOFF;
		}
	}
	// 2. 현재 필터 모드인 경우
	else if (g_ui_edit_mode == UI_EDIT_FILTER) {
		// Cutoff이면 -> Resonance로 이동
		if (g_filter_sel == FILTER_SEL_CUTOFF) {
			g_filter_sel = FILTER_SEL_RESO;
		}
		// Resonance(마지막)이면 -> 다시 ADSR 모드(A)로 복귀!
		else {
			g_ui_edit_mode = UI_EDIT_ADSR;
			g_adsr_sel = ADSR_SEL_A;
		}
	}
	// (예외 처리: 혹시 다른 모드라면 ADSR 초기화)
	else {
		g_ui_edit_mode = UI_EDIT_ADSR;
		g_adsr_sel = ADSR_SEL_A;
	}

	// [중요] 두 영역 모두 dirty 처리해야
	// 기존 박스는 지워지고, 새로운 강조가 나타납니다.
	g_ui_dirty.adsr_labels = 1;
	g_ui_dirty.filter_labels = 1;
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

    // 1. ADSR 모드일 때
    if (g_ui_edit_mode == UI_EDIT_ADSR) {
        switch (g_adsr_sel) {
        case ADSR_SEL_A:
            g_ui_adsr.attack_steps = (uint32_t) clampi((int) g_ui_adsr.attack_steps + delta, 1, 200);
            break;
        case ADSR_SEL_D:
            g_ui_adsr.decay_steps = (uint32_t) clampi((int) g_ui_adsr.decay_steps + delta, 1, 200);
            break;
        case ADSR_SEL_S:
            g_ui_adsr.sustain_level = (uint32_t) clampi((int) g_ui_adsr.sustain_level + delta, 0, 100);
            break;
        case ADSR_SEL_R:
            g_ui_adsr.release_steps = (uint32_t) clampi((int) g_ui_adsr.release_steps + delta, 1, 200);
            break;
        }
        g_ui_dirty.adsr_graph = 1; // 그래프 갱신
    }
    // 2. [추가됨] 필터 모드일 때 (여기가 핵심!)
    else if (g_ui_edit_mode == UI_EDIT_FILTER) {
        if (g_filter_sel == FILTER_SEL_CUTOFF) {
            // Cutoff 값 변경 (0 ~ 100)
            int val = (int)g_ui_cutoff + delta;
            g_ui_cutoff = (uint8_t)clampi(val, 0, 100);
        } else {
            // Resonance 값 변경 (0 ~ 100)
            int val = (int)g_ui_reso + delta;
            g_ui_reso = (uint8_t)clampi(val, 0, 100);
        }

        // (선택 사항) 만약 화면에 숫자를 표시하고 있다면 여기서 UI 갱신 플래그를 켜야 함
        // 현재는 텍스트만 있으므로 변수값만 바꾸면 Sound Engine이 알아서 읽어감
         g_ui_dirty.filter_sel = 1;
    }
    // 3. 그 외 (볼륨 모드 등)
    else {
        int v = (int) g_ui_vol + delta;
        g_ui_vol = (uint8_t) clampi(v, 0, 100);
        g_ui_dirty.volume_bar = 1;
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
	g_ui_dirty.wave_title = 1; // 화면(중앙 노트/옥타브 표시) 갱신 요청
}

// 3. 음계 변경 함수 (키패드에서 호출)
void UI_OnChangeNote(uint8_t note_index) {
    // note_index: 0=C, 1=D, 2=E, 3=F, 4=G, 5=A, 6=B
    // 12음계 변환: C=0, D=2, E=4, F=5, G=7, A=9, B=11
    const uint8_t note_map[7] = {0, 2, 4, 5, 7, 9, 11};

    if (note_index < 7) {
        g_ui_note = note_map[note_index];
        g_ui_dirty.wave_title = 1; // 화면 갱신
    }
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
				g_ui_dirty.full_redraw = 1;
			} else {
				ILI9341_Fill_Screen(BLACK);
			}
		}

		if (screen_mode == LCD_STATE_GRAPH_VIEW) {
			// 전체 다시 그리기
			if (g_ui_dirty.full_redraw) {
				g_ui_dirty.full_redraw = 0;

				ILI9341_Fill_Screen(BLACK);

				// 새로운 순서로 그리기
				draw_title_bar();           // ← 타이틀 바 추가
				draw_adsr_graph();
				draw_adsr_labels();         // ← 통합된 함수
				draw_wave_title();          // ← 파형 타이틀 추가
				draw_filter_labels();       // ← 통합된 함수
				draw_volume_bar();
				draw_wave_graph();          // ← 맨 아래로 이동

				// 나머지 플래그도 초기화
				g_ui_dirty.adsr_graph = 0;
				g_ui_dirty.adsr_labels = 0;    // ← adsr_sel 대신
				g_ui_dirty.wave_title = 0;     // ← 추가
				g_ui_dirty.wave_graph = 0;
				g_ui_dirty.filter_labels = 0;  // ← filter_sel 대신
				g_ui_dirty.volume_bar = 0;
			}

			// 부분 업데이트 (ADSR 그래프)
			if (g_ui_dirty.adsr_graph) {
				g_ui_dirty.adsr_graph = 0;
				ILI9341_Draw_Filled_Rectangle_Coord(10, ADSR_Y0, 230, ADSR_Y1, BLACK);
				draw_adsr_graph();
			}

			// 부분 업데이트 (ADSR 라벨)
			if (g_ui_dirty.adsr_labels) {
				g_ui_dirty.adsr_labels = 0;
				draw_adsr_labels();
			}

			// 부분 업데이트 (파형 타이틀)
			if (g_ui_dirty.wave_title) {
				g_ui_dirty.wave_title = 0;
				draw_wave_title();
			}

			// 부분 업데이트 (파형 그래프)
			if (g_ui_dirty.wave_graph) {
				g_ui_dirty.wave_graph = 0;
				ILI9341_Draw_Filled_Rectangle_Coord(0, WAVE_GRAPH_Y0, 240, WAVE_GRAPH_Y1, BLACK);
				draw_wave_graph();
			}

			// 부분 업데이트 (필터 라벨)
			if (g_ui_dirty.filter_labels) {
				g_ui_dirty.filter_labels = 0;
				draw_filter_labels();
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
