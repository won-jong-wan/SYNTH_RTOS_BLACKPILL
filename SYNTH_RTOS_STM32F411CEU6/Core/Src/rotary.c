/*
 * rotary.c
 *
 *  Created on: Jan 10, 2026
 *      Author: KCCISTC
 */


#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "semphr.h"
#include <stdint.h>
#include <stdio.h>
#include "user_rtos.h"

QueueHandle_t g_inputQ = NULL;
volatile int32_t g_enc_pos[2] = {0, 0};

#define STEPS_PER_DETENT 4
#define BTN_DEBOUNCE_MS  30

static inline void push_evt_from_isr(uint8_t id, uint8_t type, uint32_t data)
{
    BaseType_t hpw = pdFALSE;
    input_evt_t e;
    e.id   = id;
    e.type = type;
    e.tick = xTaskGetTickCountFromISR();

    xQueueSendFromISR(g_inputQ, &e, &hpw);
    portYIELD_FROM_ISR(hpw);
}

typedef struct {
    uint8_t  prev_ab;
    int8_t   step_acc;
    // uint32_t last_enc_tick; // 엔코더는 보통 하드웨어 노이즈 필터에 의존하거나 상태머신으로 처리
    uint8_t  btn_last;
    uint32_t btn_last_tick;
} enc_state_t;

static enc_state_t s_enc[2];

// 4상 전이 테이블
static const int8_t s_qdec[16] = {
    0, +1, -1,  0,
   -1,  0,  0, +1,
   +1,  0,  0, -1,
    0, -1, +1,  0
};

static inline uint8_t read_ab(uint8_t id)
{
    uint8_t A = 0;
    uint8_t B = 0;

    if (id == 0) {
        A = (HAL_GPIO_ReadPin(Rotary1_S1_GPIO_Port, Rotary1_S1_Pin) == GPIO_PIN_SET);
        B = (HAL_GPIO_ReadPin(Rotary1_S2_GPIO_Port, Rotary1_S2_Pin) == GPIO_PIN_SET);
    }
    // else if (id == 1) { ... }

    return (A << 1) | B;
}

static inline uint8_t read_btn(uint8_t id)
{
    if (id == 0) {
        return (HAL_GPIO_ReadPin(Rotary1_KEY_GPIO_Port, Rotary1_KEY_Pin) == GPIO_PIN_SET);
    }
    return 0; // id 1 미구현
}

static inline uint8_t btn_pressed_level(uint8_t raw)
{
    // Low Active 가정 (눌림=0 -> 1로 변환)
    return (raw == 0) ? 1 : 0;
}

void handle_enc_ab(const input_evt_t *e)
{
    uint8_t id = e->id;
    uint8_t ab = read_ab(id);

    uint8_t prev = s_enc[id].prev_ab;
    s_enc[id].prev_ab = ab;

    int8_t d = s_qdec[(prev << 2) | ab];
    if (d == 0) return;

    s_enc[id].step_acc += d;
    if (s_enc[id].step_acc >= STEPS_PER_DETENT) {
        s_enc[id].step_acc = 0;
        g_enc_pos[id] += 1;
    } else if (s_enc[id].step_acc <= -STEPS_PER_DETENT) {
        s_enc[id].step_acc = 0;
        g_enc_pos[id] -= 1;
    }
}

// [수정됨] 디바운스 로직 개선 및 Tick 업데이트 추가
void handle_btn_edge(const input_evt_t *e)
{
    uint8_t id = e->id;
    uint8_t raw = read_btn(id);
    uint8_t pressed = btn_pressed_level(raw);
    uint32_t now = e->tick;

    // 디바운스 타임 체크
    if ((now - s_enc[id].btn_last_tick) < BTN_DEBOUNCE_MS) return;

    // 상태 변화가 있을 때만 처리
    if (pressed != s_enc[id].btn_last) {
        s_enc[id].btn_last_tick = now; // [중요] 마지막 변경 시간 업데이트
        s_enc[id].btn_last = pressed;

        if (pressed) {
            printf("ENC%u BTN DOWN\r\n", (unsigned)id);
        }
    }
}

void RotaryTasks_Init(void)
{
    static uint8_t started = 0;
    if (started) return;
    started = 1;

    g_inputQ = xQueueCreate(32, sizeof(input_evt_t));
    configASSERT(g_inputQ);

    BaseType_t ok = xTaskCreate(InputTask, "InputTask", 512, NULL, tskIDLE_PRIORITY + 2, NULL);
    configASSERT(ok == pdPASS);
}

void InputTask(void *arg)
{
    for (int i=0; i<2; i++) {
        s_enc[i].prev_ab = read_ab(i);
        s_enc[i].step_acc = 0;
        // s_enc[i].last_enc_tick = 0;
        s_enc[i].btn_last = btn_pressed_level(read_btn(i));
        s_enc[i].btn_last_tick = 0;
    }

    int32_t last0 = g_enc_pos[0];
    int32_t last1 = g_enc_pos[1];
    input_evt_t e;

    for (;;) {
        if (xQueueReceive(g_inputQ, &e, portMAX_DELAY) == pdTRUE) {
            if (e.type == EVT_ENC_AB)        handle_enc_ab(&e);
            else if (e.type == EVT_BTN_EDGE) handle_btn_edge(&e);

            if (g_enc_pos[0] != last0 || g_enc_pos[1] != last1) {
                last0 = g_enc_pos[0];
                last1 = g_enc_pos[1];
                printf("ENC1=%ld ENC2=%ld\r\n", (long)last0, (long)last1);
            }
        }
    }
}

// [수정됨] EXTI Callback 주석 해제 (main.c 등에 중복 정의 없어야 함)
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    if (g_inputQ == NULL) return;

    // ENC1 A/B
    if (GPIO_Pin == Rotary1_S1_Pin || GPIO_Pin == Rotary1_S2_Pin) {
        push_evt_from_isr(0, EVT_ENC_AB, 0);
        return;
    }

    // ENC1 KEY
    if (GPIO_Pin == Rotary1_KEY_Pin) {
        push_evt_from_isr(0, EVT_BTN_EDGE, 0);
        return;
    }

    // ENC2 관련 핀 (필요시 주석 해제)
    /*
    if (GPIO_Pin == Rotary2_S1_Pin || GPIO_Pin == Rotary2_S2_Pin) {
        push_evt_from_isr(1, EVT_ENC_AB, 0);
        return;
    }
    if (GPIO_Pin == Rotary2_KEY_Pin) {
        push_evt_from_isr(1, EVT_BTN_EDGE, 0);
        return;
    }
    */
}
