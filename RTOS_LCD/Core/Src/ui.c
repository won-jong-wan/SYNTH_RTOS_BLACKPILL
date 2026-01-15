/*
 * ui.c - UI 디자인 교체본
 *
 *  Created on: Jan 14, 2026
 *      Author: 영교
 */

#include "ui.h"
#include "main.h"
#include "ILI9341_STM32_Driver.h"
#include "ILI9341_GFX.h"
#include <string.h>
#include <stdio.h>
#include <math.h>
#include "task.h"
#include "user_rtos.h"

TaskHandle_t lcdTaskHandle = NULL;
QueueHandle_t lcdQueueHandle = NULL;
LcdState_t currentLcdState = LCD_STATE_GRAPH_VIEW;

#define LCD_W   240
#define LCD_H   320

// ==================== UI 좌표 정의 (새로운 디자인) ====================
#define TITLE_Y0        0
#define TITLE_Y1        30

#define ADSR_Y0         50
#define ADSR_Y1         110

#define ADSR_LABEL_Y    135

#define WAVE_TITLE_Y0   160
#define WAVE_TITLE_Y1   185

#define FILTER_Y        195

#define VOL_Y           220

#define WAVE_GRAPH_Y0   240
#define WAVE_GRAPH_Y1   295

uint8_t sin_samples[1024];

static void LCD_Task(void *argument);
static void Generate_Sine_Samples(void);
static void draw_main_dashboard(void);
static void draw_line(int x0, int y0, int x1, int y1, uint16_t color);

// UI 데이터
volatile UI_ADSR_t g_ui_adsr = {
    .attack_steps = 40,
    .decay_steps = 30,
    .sustain_level = 50,
    .release_steps = 60
};

volatile UI_Wave_t g_ui_wave = UI_WAVE_SINE;
const char* wave_names[] = {"SINE", "SQUARE", "TRIANGLE"};

// 음계 정보
typedef struct {
    uint8_t note_idx;  // 0~6 (C~B)
    uint8_t octave;    // 2~3
} NoteInfo_t;
volatile NoteInfo_t current_note = {0, 2}; // C2

volatile uint8_t g_ui_dirty = 1;

// ================== UI 편집 모드 + 선택 dirty ==================
typedef enum {
    UI_EDIT_ADSR = 0,
    UI_EDIT_FILTER,
    UI_EDIT_VOLUME
} UI_EditMode_t;

static void UI_SelectVolumeMode(void);
volatile UI_EditMode_t g_ui_edit_mode = UI_EDIT_ADSR;

// 선택 dirty
volatile uint8_t g_ui_adsr_sel_dirty = 1;
volatile uint8_t g_ui_filter_sel_dirty = 1;

// 엔코더/버튼 처리 함수
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

    BaseType_t result = xTaskCreate(LCD_Task, "LCDTask", 2048, NULL,
                                    tskIDLE_PRIORITY + 1, &lcdTaskHandle);

    if (result != pdPASS) {
        Error_Handler();
    }
}

static void Generate_Sine_Samples(void) {
    for (int i = 0; i < 1024; i++) {
        sin_samples[i] = (uint8_t)(127 + 100 * sin(2.0 * M_PI * i / 1024.0 * 5.0));
    }
}

// ==================== 화면 그리기 (새로운 UI 디자인) ====================
static void draw_main_dashboard(void) {
    ILI9341_Fill_Screen(BLACK);
    vTaskDelay(pdMS_TO_TICKS(10));

    ILI9341_Draw_Filled_Rectangle_Coord(0, 0, 240, 30, GREEN);
    ILI9341_Draw_Text("SYSTEM READY", 48, 10, BLACK, 2, GREEN);

    ILI9341_Draw_Text("Welcome to", 60, 60, WHITE, 2, BLACK);
    ILI9341_Draw_Text("SYNTH RTOS", 30, 90, YELLOW, 3, BLACK);
    ILI9341_Draw_Text("BLACKPILL", 39, 130, CYAN, 3, BLACK);

    ILI9341_Draw_Horizontal_Line(20, 180, 200, WHITE);
    ILI9341_Draw_Text("Press Button", 84, 195, LIGHTGREY, 1, BLACK);
}

static void draw_title_bar(void) {
    ILI9341_Draw_Filled_Rectangle_Coord(0, TITLE_Y0, 240, TITLE_Y1, CYAN);
    ILI9341_Draw_Text("ADSR ENVELOPE", 42, 8, BLACK, 2, CYAN);
    ILI9341_Draw_Horizontal_Line(0, TITLE_Y1, 240, WHITE);
}

// ADSR 선택
typedef enum {
    ADSR_SEL_A = 0,
    ADSR_SEL_D,
    ADSR_SEL_S,
    ADSR_SEL_R
} UI_ADSR_Select_t;
volatile UI_ADSR_Select_t g_adsr_sel = ADSR_SEL_A;

// FILTER 선택
typedef enum {
    FILTER_SEL_CUTOFF = 0,
    FILTER_SEL_RESO
} UI_Filter_Select_t;
volatile UI_Filter_Select_t g_filter_sel = FILTER_SEL_CUTOFF;

// 사운드 엔진 값
volatile uint8_t g_ui_note = 0;
volatile uint8_t g_ui_oct = 4;
volatile uint8_t g_ui_vol = 50;
volatile uint8_t g_ui_note_dirty = 1;
volatile uint8_t g_ui_vol_dirty = 1;

// FILTER 값
volatile uint8_t g_ui_cutoff = 50;
volatile uint8_t g_ui_reso = 30;

// 그래프 dirty
volatile uint8_t g_ui_adsr_dirty = 1;
volatile uint8_t g_ui_filter_dirty = 1;

static void draw_note_display(void);

// ==================== ADSR 그래프 (픽셀 기반) ====================
static void draw_adsr_graph(void) {
    // 그래프 영역만 지우기
    ILI9341_Draw_Filled_Rectangle_Coord(0, ADSR_Y0, 240, ADSR_Y1, BLACK);

    int w = 240;
    int h = ADSR_Y1 - ADSR_Y0;

    int wS = w / 4;
    uint32_t A = g_ui_adsr.attack_steps;
    uint32_t D = g_ui_adsr.decay_steps;
    uint32_t R = g_ui_adsr.release_steps;
    uint32_t denom = (A + D + R);
    if (denom == 0) denom = 1;

    int avail = (w - wS);
    int wA = (int)((uint64_t)avail * A / denom);
    int wD = (int)((uint64_t)avail * D / denom);
    int wR = (int)((uint64_t)avail * R / denom);

    int sum = wA + wD + wS + wR;
    if (sum != w) wR += (w - sum);

    int y_base = ADSR_Y1;
    int y_peak = ADSR_Y0;
    int h_pix = h - 1;
    int y_sus = ADSR_Y0 + ((100 - g_ui_adsr.sustain_level) * h_pix) / 100;

    if (y_sus < ADSR_Y0) y_sus = ADSR_Y0;
    if (y_sus > ADSR_Y1) y_sus = ADSR_Y1;

    int xA0 = 0, yA0 = y_base;
    int xA1 = wA, yA1 = y_peak;
    int xD1 = xA1 + wD, yD1 = y_sus;
    int xS1 = xD1 + wS, yS1 = y_sus;
    int xR1 = xS1 + wR, yR1 = y_base;

    if (xR1 > 239) xR1 = 239;
    if (xA1 > 239) xA1 = 239;
    if (xD1 > 239) xD1 = 239;
    if (xS1 > 239) xS1 = 239;

    // 픽셀로 그리기
    for (int x = 0; x < 240; x++) {
        int y;
        if (x <= xA1) {
            y = y_base - ((y_base - y_peak) * x) / (xA1 > 0 ? xA1 : 1);
        } else if (x <= xD1) {
            int dx = x - xA1;
            int dw = xD1 - xA1;
            y = y_peak - ((y_peak - y_sus) * dx) / (dw > 0 ? dw : 1);
        } else if (x <= xS1) {
            y = y_sus;
        } else {
            int dx = x - xS1;
            int dw = xR1 - xS1;
            y = y_sus - ((y_sus - y_base) * dx) / (dw > 0 ? dw : 1);
        }

        if (y < ADSR_Y0) y = ADSR_Y0;
        if (y > ADSR_Y1) y = ADSR_Y1;

        ILI9341_Draw_Pixel(x, (uint16_t)y, YELLOW);
    }
}

static void draw_adsr_labels(void) {
    // 라벨 영역만 지우기
    ILI9341_Draw_Filled_Rectangle_Coord(0, ADSR_LABEL_Y, 240, ADSR_LABEL_Y + 20, BLACK);

    // 라벨 색상 결정 (선택된 것만 RED)
    uint16_t color_A = (g_adsr_sel == ADSR_SEL_A) ? RED : WHITE;
    uint16_t color_D = (g_adsr_sel == ADSR_SEL_D) ? RED : WHITE;
    uint16_t color_S = (g_adsr_sel == ADSR_SEL_S) ? RED : WHITE;
    uint16_t color_R = (g_adsr_sel == ADSR_SEL_R) ? RED : WHITE;

    ILI9341_Draw_Text("A", 30, ADSR_LABEL_Y, color_A, 2, BLACK);
    ILI9341_Draw_Text("D", 85, ADSR_LABEL_Y, color_D, 2, BLACK);
    ILI9341_Draw_Text("S", 140, ADSR_LABEL_Y, color_S, 2, BLACK);
    ILI9341_Draw_Text("R", 195, ADSR_LABEL_Y, color_R, 2, BLACK);
}

static void draw_wave_title(void) {
    uint16_t bar_color = (g_ui_edit_mode == UI_EDIT_FILTER) ? RED : MAGENTA;

    // 타이틀 바 영역만 지우고 다시 그리기
    ILI9341_Draw_Filled_Rectangle_Coord(0, WAVE_TITLE_Y0, 240, WAVE_TITLE_Y1, bar_color);

    // 파형 이름
    const char *wave_name = wave_names[g_ui_wave];
    ILI9341_Draw_Text(wave_name, 5, 165, WHITE, 2, bar_color);

    // 음계 표시
    draw_note_display();
}

static void draw_note_display(void) {
    const char *notes_list[] = {"C", "D", "E", "F", "G", "A", "B"};
    char note_buf[12];

    uint16_t bar_color = (g_ui_edit_mode == UI_EDIT_FILTER) ? RED : MAGENTA;

    sprintf(note_buf, "NOTE:%s%d", notes_list[current_note.note_idx], current_note.octave);
    ILI9341_Draw_Text(note_buf, 145, 165, YELLOW, 2, bar_color);
}

static void draw_wave_graph(void) {
    // 파형 그래프 영역만 지우기
    ILI9341_Draw_Filled_Rectangle_Coord(0, WAVE_GRAPH_Y0, 240, WAVE_GRAPH_Y1, BLACK);

    int w = 240;
    int h = WAVE_GRAPH_Y1 - WAVE_GRAPH_Y0;
    int midY = WAVE_GRAPH_Y0 + h / 2;
    int ampPix = (h * 35) / 100;

    for (int x = 0; x < w; x++) {
        int phase2 = (x * 2048) / w;
        int idx = phase2 & 1023;

        int y = midY;

        if (g_ui_wave == UI_WAVE_SINE) {
            int s = (int)sin_samples[idx] - 127;
            y = midY - (s * ampPix) / 100;
        } else if (g_ui_wave == UI_WAVE_SQUARE) {
            int s = (int)sin_samples[idx] - 127;
            y = midY - ((s >= 0) ? ampPix : -ampPix);
        } else { // TRIANGLE
            int p = idx;
            int tri;
            if (p < 512) {
                tri = -ampPix + (p * (2 * ampPix)) / 511;
            } else {
                tri = +ampPix - ((p - 512) * (2 * ampPix)) / 511;
            }
            y = midY - tri;
        }

        if (y < WAVE_GRAPH_Y0) y = WAVE_GRAPH_Y0;
        if (y > WAVE_GRAPH_Y1) y = WAVE_GRAPH_Y1;

        ILI9341_Draw_Pixel(x, (uint16_t)y, GREEN);
    }
}

static void draw_filter_labels(void) {
    // 필터 라벨 영역만 지우기
    ILI9341_Draw_Filled_Rectangle_Coord(0, FILTER_Y, 240, FILTER_Y + 20, BLACK);

    uint16_t color_reso = (g_filter_sel == FILTER_SEL_RESO) ? RED : WHITE;
    uint16_t color_cutoff = (g_filter_sel == FILTER_SEL_CUTOFF) ? RED : WHITE;

    ILI9341_Draw_Text("RESO", 45, FILTER_Y, color_reso, 2, BLACK);
    ILI9341_Draw_Text("CUTOFF", 125, FILTER_Y, color_cutoff, 2, BLACK);
}

static void draw_volume_bar(void) {
    // 볼륨 영역만 지우기
    ILI9341_Draw_Filled_Rectangle_Coord(0, VOL_Y, 240, VOL_Y + 15, BLACK);

    uint16_t vol_color = (g_ui_edit_mode == UI_EDIT_VOLUME) ? RED : WHITE;

    ILI9341_Draw_Text("Vol", 15, VOL_Y, vol_color, 1, BLACK);

    // 막대 게이지
    for (int i = 0; i < 10; i++) {
        uint16_t bar_x = 45 + (i * 17);
        if (i < (g_ui_vol / 10)) {
            ILI9341_Draw_Filled_Rectangle_Coord(bar_x, VOL_Y, bar_x + 13, VOL_Y + 8, vol_color);
        } else {
            ILI9341_Draw_Rectangle(bar_x, VOL_Y, 13, 8, DARKGREY);
        }
    }

    char vol_buf[6];
    sprintf(vol_buf, "%d  ", g_ui_vol);
    ILI9341_Draw_Text(vol_buf, 215, VOL_Y, vol_color, 1, BLACK);
}

static void draw_line(int x0, int y0, int x1, int y1, uint16_t color) {
    int dx = (x1 > x0) ? (x1 - x0) : (x0 - x1);
    int sx = (x0 < x1) ? 1 : -1;
    int dy = (y1 > y0) ? (y0 - y1) : (y1 - y0);
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx + dy;

    while (1) {
        if (x0 >= 0 && x0 < LCD_W && y0 >= 0 && y0 < LCD_H) {
            ILI9341_Draw_Pixel((uint16_t)x0, (uint16_t)y0, color);
        }
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

static void UI_SelectVolumeMode(void) {
    g_ui_edit_mode = UI_EDIT_VOLUME;
    g_ui_vol_dirty = 1;
    g_ui_adsr_sel_dirty = 1;
    g_ui_filter_sel_dirty = 1;
}

// ================== 값/선택 처리 로직 ==================
static int clampi(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static void UI_MoveAdsrSelect(void) {
    g_ui_edit_mode = UI_EDIT_ADSR;
    g_adsr_sel = (UI_ADSR_Select_t)((g_adsr_sel + 1) % 4);
    g_ui_adsr_sel_dirty = 1;
}

static void UI_ToggleFilterSelect(void) {
    g_ui_edit_mode = UI_EDIT_FILTER;
    g_filter_sel = (g_filter_sel == FILTER_SEL_CUTOFF) ? FILTER_SEL_RESO : FILTER_SEL_CUTOFF;
    g_ui_filter_sel_dirty = 1;
}

void UI_OnEncoderDelta(int delta) {
    if (delta == 0) return;

    if (g_ui_edit_mode == UI_EDIT_ADSR) {
        switch (g_adsr_sel) {
        case ADSR_SEL_A:
            g_ui_adsr.attack_steps = (uint32_t)clampi((int)g_ui_adsr.attack_steps + delta, 1, 200);
            break;
        case ADSR_SEL_D:
            g_ui_adsr.decay_steps = (uint32_t)clampi((int)g_ui_adsr.decay_steps + delta, 1, 200);
            break;
        case ADSR_SEL_S:
            g_ui_adsr.sustain_level = (uint32_t)clampi((int)g_ui_adsr.sustain_level + delta, 0, 100);
            break;
        case ADSR_SEL_R:
            g_ui_adsr.release_steps = (uint32_t)clampi((int)g_ui_adsr.release_steps + delta, 1, 200);
            break;
        }
        g_ui_adsr_dirty = 1;
    } else if (g_ui_edit_mode == UI_EDIT_FILTER) {
        if (g_filter_sel == FILTER_SEL_CUTOFF) {
            g_ui_cutoff = (uint8_t)clampi((int)g_ui_cutoff + delta, 0, 100);
        } else {
            g_ui_reso = (uint8_t)clampi((int)g_ui_reso + delta, 0, 100);
        }
        g_ui_filter_dirty = 1;
    } else { // UI_EDIT_VOLUME
        int v = (int)g_ui_vol + delta;
        v = clampi(v, 0, 100);
        g_ui_vol = (uint8_t)v;
        g_ui_vol_dirty = 1;
    }
}

// ==================== LCD Task ====================
static void LCD_Task(void *argument) {
    uint32_t received;
    uint8_t screen_mode = LCD_STATE_GRAPH_VIEW;
    uint8_t last_screen_mode = 255;

    for (;;) {
        if (xQueueReceive(lcdQueueHandle, &received, 0) == pdTRUE) {
            screen_mode = (uint8_t)received;

            if (screen_mode == LCD_STATE_GRAPH_VIEW) {
                g_ui_dirty = 1;
            } else {
                ILI9341_Fill_Screen(BLACK);
            }
        }

        if (screen_mode == LCD_STATE_GRAPH_VIEW) {
            // 전체 다시 그리기
            if (g_ui_dirty) {
                g_ui_dirty = 0;

                ILI9341_Fill_Screen(BLACK);
                draw_title_bar();
                draw_adsr_graph();
                draw_adsr_labels();
                draw_wave_title();
                draw_wave_graph();
                draw_filter_labels();
                draw_volume_bar();

                g_ui_adsr_dirty = 0;
                g_ui_filter_dirty = 0;
                g_ui_adsr_sel_dirty = 0;
                g_ui_filter_sel_dirty = 0;
                g_ui_note_dirty = 0;
                g_ui_vol_dirty = 0;
            }

            // 부분 업데이트
            if (g_ui_adsr_dirty) {
                g_ui_adsr_dirty = 0;
                ILI9341_Draw_Filled_Rectangle_Coord(0, ADSR_Y0, 240, ADSR_Y1, BLACK);
                draw_adsr_graph();
            }

            if (g_ui_filter_dirty) {
                g_ui_filter_dirty = 0;
                ILI9341_Draw_Filled_Rectangle_Coord(0, WAVE_GRAPH_Y0, 240, WAVE_GRAPH_Y1, BLACK);
                draw_wave_graph();
            }

            if (g_ui_adsr_sel_dirty) {
                g_ui_adsr_sel_dirty = 0;
                draw_adsr_labels();
            }

            if (g_ui_filter_sel_dirty) {
                g_ui_filter_sel_dirty = 0;
                draw_filter_labels();
            }

            if (g_ui_vol_dirty) {
                g_ui_vol_dirty = 0;
                draw_volume_bar();
            }

            if (g_ui_note_dirty) {
                g_ui_note_dirty = 0;
                draw_wave_title(); // 음계가 파형 타이틀에 포함됨
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
    static uint32_t last_tick_ui = 0;

    if (HAL_GetTick() - last_tick_ui < 50) return;
    last_tick_ui = HAL_GetTick();

    if (GPIO_PIN == Rotary1_KEY_Pin) {
        UI_MoveAdsrSelect();
        return;
    }
}
