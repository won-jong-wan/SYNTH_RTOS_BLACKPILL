/*
 * ui.h
 */

#ifndef INC_UI_H_
#define INC_UI_H_

#include "stm32f4xx_hal.h"
#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"

// ===== Dirty Flags 구조체 =====
typedef struct {
    uint8_t full_redraw;       // 전체 화면 다시 그리기
    uint8_t adsr_graph;        // ADSR 그래프만
    uint8_t adsr_sel;          // ADSR 선택 강조만
    uint8_t wave_graph;        // 파형 그래프만
    uint8_t filter_sel;        // 필터 선택 강조만
    uint8_t note_display;      // 음계 표시만
    uint8_t volume_bar;        // 볼륨 바만
} UI_DirtyFlags_t;

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

// [추가] 시각화용 버퍼 사이즈 (화면 너비와 비슷하게)
#define VIS_BUF_SIZE 240

// 전역 변수
extern TaskHandle_t lcdTaskHandle;
extern QueueHandle_t lcdQueueHandle;
extern LcdState_t currentLcdState;

extern volatile UI_DirtyFlags_t g_ui_dirty;
extern volatile UI_ADSR_t g_ui_adsr;
extern volatile UI_Wave_t g_ui_wave;
extern volatile UI_EditMode_t g_ui_edit_mode;
extern volatile UI_ADSR_Select_t g_adsr_sel;
extern volatile UI_Filter_Select_t g_filter_sel;

extern volatile uint8_t g_ui_note;
extern volatile uint8_t g_ui_oct;
extern volatile uint8_t g_ui_vol;
extern volatile uint8_t g_ui_cutoff;
extern volatile uint8_t g_ui_reso;

//extern uint8_t sin_samples[1024];

// [추가] 사운드 엔진이 채우고, UI가 읽을 버퍼
extern volatile int16_t g_vis_buffer[VIS_BUF_SIZE];

// 함수 선언
void display_init(void);
void UI_Init(void);
void UI_OnEncoderDelta(int delta);
void UI_OnChangeVolume(int delta);    // Rotary 2 (Volume)
void UI_OnChangeOctave(int delta);    // 버튼용 (Octave)

#endif /* INC_UI_H_ */
