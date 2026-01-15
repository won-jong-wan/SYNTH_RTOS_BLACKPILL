/*
 * ui.h
 */

#ifndef INC_UI_H_
#define INC_UI_H_

#include "stm32f4xx_hal.h"
#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"

// LCD 상태
typedef enum {
    LCD_STATE_INIT = 0,
    LCD_STATE_MAIN_DASH,
    LCD_STATE_GRAPH_VIEW
} LcdState_t;

// ADSR 파라미터
typedef struct {
    uint32_t attack_steps;
    uint32_t decay_steps;
    uint32_t sustain_level;
    uint32_t release_steps;
} UI_ADSR_t;

// 파형 타입
typedef enum {
    UI_WAVE_SINE = 0,
    UI_WAVE_SQUARE,
    UI_WAVE_SAW
} UI_Wave_t;

// 편집 모드
typedef enum {
    UI_EDIT_ADSR = 0,
    UI_EDIT_FILTER,
    UI_EDIT_VOLUME
} UI_EditMode_t;

// ADSR 선택
typedef enum {
    ADSR_SEL_A = 0,
    ADSR_SEL_D,
    ADSR_SEL_S,
    ADSR_SEL_R
} UI_ADSR_Select_t;

// 필터 선택
typedef enum {
    FILTER_SEL_CUTOFF = 0,
    FILTER_SEL_RESO
} UI_Filter_Select_t;

// 음계 정보
typedef struct {
    uint8_t note_idx;  // 0~6 (C~B)
    uint8_t octave;    // 2~3
} NoteInfo_t;

// ===== RTOS 핸들 =====
extern TaskHandle_t lcdTaskHandle;
extern QueueHandle_t lcdQueueHandle;
extern LcdState_t currentLcdState;

// ===== UI 데이터 =====
extern volatile UI_ADSR_t g_ui_adsr;
extern volatile UI_Wave_t g_ui_wave;
extern volatile UI_EditMode_t g_ui_edit_mode;
extern volatile UI_ADSR_Select_t g_adsr_sel;
extern volatile UI_Filter_Select_t g_filter_sel;
extern volatile NoteInfo_t current_note;

// ===== Dirty Flags (개별) =====
extern volatile uint8_t g_ui_dirty;
extern volatile uint8_t g_ui_adsr_dirty;
extern volatile uint8_t g_ui_adsr_sel_dirty;
extern volatile uint8_t g_ui_filter_dirty;
extern volatile uint8_t g_ui_filter_sel_dirty;
extern volatile uint8_t g_ui_note_dirty;
extern volatile uint8_t g_ui_vol_dirty;

// ===== UI 표시 값 =====
extern volatile uint8_t g_ui_note;
extern volatile uint8_t g_ui_oct;
extern volatile uint8_t g_ui_vol;
extern volatile uint8_t g_ui_cutoff;
extern volatile uint8_t g_ui_reso;

// ===== 샘플 데이터 =====
extern uint8_t sin_samples[1024];

// ===== 함수 선언 =====
void display_init(void);
void UI_Init(void);
void UI_OnEncoderDelta(int delta);

#endif /* INC_UI_H_ */
