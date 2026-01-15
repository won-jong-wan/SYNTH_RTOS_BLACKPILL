/*
 * ui.h
 *
 *  Created on: Jan 14, 2026
 *      Author: 환중
 */

#ifndef INC_UI_H_
#define INC_UI_H_

#include <stdint.h>
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

/* ===== 화면 상태 정의 ===== */
typedef enum {
    LCD_STATE_INIT = 0,
    LCD_STATE_MAIN_DASH,
    LCD_STATE_GRAPH_VIEW
} LcdState_t;

/* ===== 전역 변수 (외부 접근용) ===== */
extern TaskHandle_t lcdTaskHandle;
extern QueueHandle_t lcdQueueHandle;
extern LcdState_t currentLcdState;
extern uint8_t sin_samples[1024];

typedef enum {
    UI_WAVE_SINE = 0,
    UI_WAVE_SAW,
    UI_WAVE_SQUARE,
    UI_WAVE_TRI
} UI_Wave_t;

typedef struct {
    uint32_t attack_steps;
    uint32_t decay_steps;
    uint8_t sustain_level;   // 0.0~1.0
    uint32_t release_steps;
} UI_ADSR_t;

extern volatile UI_ADSR_t g_ui_adsr;
extern volatile UI_Wave_t g_ui_wave;
extern volatile uint8_t   g_ui_dirty;
extern volatile uint8_t g_ui_vol;     // 0~100
extern volatile uint8_t g_ui_cutoff;  // 0~100
extern volatile uint8_t g_ui_reso;    // 0~100

/* ===== 초기화 함수 ===== */
void UI_Init(void);
void UI_OnEncoderDelta(int delta); // rotary.c에서 호출

#endif /* INC_UI_H_ */
