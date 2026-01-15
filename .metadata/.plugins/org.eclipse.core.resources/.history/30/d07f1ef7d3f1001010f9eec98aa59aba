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

typedef struct {
    uint8_t full_redraw;       // 전체 화면 다시 그리기
    uint8_t adsr_graph;        // ADSR 그래프만
    uint8_t adsr_labels;       // ADSR 라벨 강조만
    uint8_t wave_title;        // 파형 타이틀 바
    uint8_t wave_graph;        // 파형 그래프
    uint8_t filter_labels;     // 필터 라벨
    uint8_t volume_bar;        // 볼륨 게이지
    uint8_t note_display;      // 음계 표시
} UI_DirtyFlags_t;

/* ===== 화면 상태 정의 ===== */
typedef enum {
    LCD_STATE_INIT = 0,
	LCD_STATE_MAIN_DASH,
	LCD_STATE_ADSR_VIEW
} LcdState_t;

typedef struct {
    uint8_t note_idx;   // 0~6 (C,D,E,F,G,A,B)
    uint8_t octave;     // 2 or 3
} NoteInfo_t;

/* ===== 전역 변수 (외부 접근용) ===== */
extern TaskHandle_t lcdTaskHandle;
extern QueueHandle_t lcdQueueHandle;
extern LcdState_t currentLcdState;

extern volatile UI_DirtyFlags_t g_ui_dirty;
extern volatile uint8_t selected_adsr_idx;
extern volatile uint8_t volume_val;
extern volatile uint8_t selected_wave_type;
extern volatile NoteInfo_t current_note;

extern const char* wave_names[];
extern uint8_t adsr_samples[1024];
extern uint8_t sin_samples[1024];

/* ===== 초기화 함수 ===== */
void UI_Init(void);

#endif /* INC_UI_H_ */
