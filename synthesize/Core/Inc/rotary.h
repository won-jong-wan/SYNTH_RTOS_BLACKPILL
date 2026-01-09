/*
 * rotary.h
 *
 *  Created on: Jan 9, 2026
 *      Author: KCCISTC
 */

#ifndef INC_ROTARY_H_
#define INC_ROTARY_H_
#define ENC1_S1_PORT   GPIOD
#define ENC1_S1_PIN    GPIO_PIN_5
#define ENC1_S2_PORT   GPIOD
#define ENC1_S2_PIN    GPIO_PIN_4
#define ENC1_KEY_PORT GPIOD
#define ENC1_KEY_PIN  GPIO_PIN_3

#define ENC2_S1_PORT   GPIOG
#define ENC2_S1_PIN    GPIO_PIN_9
#define ENC2_S2_PORT   GPIOD
#define ENC2_S2_PIN    GPIO_PIN_7
#define ENC2_KEY_PORT GPIOD
#define ENC2_KEY_PIN  GPIO_PIN_6
#pragma once
#include "FreeRTOS.h"
#include "queue.h"
#include <stdint.h>
void InputTask(void *arg);
extern QueueHandle_t g_inputQ;
typedef enum {
    EVT_ENC_AB = 0,
    EVT_BTN_EDGE = 1,
} evt_type_t;

typedef struct {
    uint8_t    id;     // 0: enc1, 1: enc2
    evt_type_t type;
    uint8_t    v;      // AB(2bit) or BTN(0/1)
    uint32_t   tick;   // xTaskGetTickCountFromISR()
} input_evt_t;


// 앱에서 쓰고 싶으면 공개
extern volatile int32_t g_enc_pos[2];
#endif /* INC_ROTARY_H_ */
