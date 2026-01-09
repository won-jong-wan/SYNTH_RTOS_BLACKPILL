/*
 * rotary.c
 *
 *  Created on: Jan 9, 2026
 *      Author: KCCISTC
 */
#include "rotary.h"
#include "stm32f4xx_hal.h"
#include "FreeRTOS.h"
#include "task.h"

QueueHandle_t g_inputQ = NULL;
volatile int32_t g_enc_pos[2] = {0, 0};

#define STEPS_PER_DETENT 4
#define BTN_DEBOUNCE_MS  30
#define ENC_GLITCH_MS     2   // 너무 민감하면 1~3ms 조절

typedef struct {
    uint8_t  prev_ab;
    int8_t   step_acc;
    uint32_t last_enc_tick;
    uint8_t  btn_last;
    uint32_t btn_last_tick;
} enc_state_t;

static enc_state_t s_enc[2];

// 4상 전이 테이블: index = (prev<<2)|cur
// CW: 00->01->11->10->00 를 +1로, 반대는 -1로
static const int8_t s_qdec[16] = {
/* prev 00 */  0, +1, -1,  0,
/* prev 01 */ -1,  0,  0, +1,
/* prev 10 */ +1,  0,  0, -1,
/* prev 11 */  0, -1, +1,  0
};

static inline uint8_t read_ab(uint8_t id)
{
    GPIO_TypeDef *ap = (id==0) ? ENC1_S1_PORT : ENC2_S1_PORT;
    uint16_t      a  = (id==0) ? ENC1_S1_PIN  : ENC2_S1_PIN;
    GPIO_TypeDef *bp = (id==0) ? ENC1_S2_PORT : ENC2_S2_PORT;
    uint16_t      b  = (id==0) ? ENC1_S2_PIN  : ENC2_S2_PIN;

    uint8_t A = (HAL_GPIO_ReadPin(ap, a) == GPIO_PIN_SET) ? 1 : 0;
    uint8_t B = (HAL_GPIO_ReadPin(bp, b) == GPIO_PIN_SET) ? 1 : 0;
    return (A<<1) | B;
}

static inline uint8_t read_btn(uint8_t id)
{
    GPIO_TypeDef *pp = (id==0) ? ENC1_KEY_PORT : ENC2_KEY_PORT;
    uint16_t      pn = (id==0) ? ENC1_KEY_PIN  : ENC2_KEY_PIN;
    return (HAL_GPIO_ReadPin(pp, pn) == GPIO_PIN_SET) ? 1 : 0;
}

// 너 버튼이 Pull-up이고 "눌림=0"이면 이걸로 논리 통일
static inline uint8_t btn_pressed_level(uint8_t raw)
{
    // raw=1이면 high. 눌림이 low면 반전
    return (raw == 0) ? 1 : 0; // pressed=1, released=0
}
void handle_enc_ab(const input_evt_t *e)
{
    uint8_t id = e->id;
    uint8_t ab = read_ab(id);

    uint8_t prev = s_enc[id].prev_ab;
    s_enc[id].prev_ab = ab;

    int8_t d = s_qdec[(prev << 2) | ab];   // ✅ 여기 수정
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

void handle_btn_edge(const input_evt_t *e)
{
    uint8_t id = e->id;

    uint8_t raw = read_btn(id);
    uint8_t pressed = btn_pressed_level(raw);

    uint32_t now = e->tick;
    if (now - s_enc[id].btn_last_tick < BTN_DEBOUNCE_MS) return;

    if (pressed && pressed != s_enc[id].btn_last) {
        printf("ENC%u BTN DOWN\r\n", (unsigned)id);
    }
    s_enc[id].btn_last = pressed;
}

void InputTask(void *arg)
{
    for (int i=0; i<2; i++) {
        s_enc[i].prev_ab = read_ab(i);
        s_enc[i].step_acc = 0;
        s_enc[i].last_enc_tick = 0;
        s_enc[i].btn_last = btn_pressed_level(read_btn(i));
        s_enc[i].btn_last_tick = 0;
    }

    int32_t last0 = g_enc_pos[0];
    int32_t last1 = g_enc_pos[1];

    input_evt_t e;
    for (;;) {
        if (xQueueReceive(g_inputQ, &e, portMAX_DELAY) == pdTRUE) {

            if (e.type == EVT_ENC_AB)       handle_enc_ab(&e);
            else if (e.type == EVT_BTN_EDGE) handle_btn_edge(&e);

            if (g_enc_pos[0] != last0 || g_enc_pos[1] != last1) {
                last0 = g_enc_pos[0];
                last1 = g_enc_pos[1];
                printf("ENC1=%ld ENC2=%ld\r\n", (long)last0, (long)last1);
            }
        }
    }
}

