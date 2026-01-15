/*
 * rotary.c
 *
 * Created on: Jan 10, 2026
 * Author: KCCISTC
 */

#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include <stdio.h>
#include <stdlib.h>
#include "user_rtos.h"
#include "ui.h"

// 엔코더 감도 설정 (2: 민감함, 4: 둔감함/안정적)
#define ENC_STEPS_PER_NOTCH 2

// 전역 변수 (외부에서 참조한다고 가정)
volatile int32_t g_enc_pos[2] = { 0, 0 };

// --- Helper Functions ---
static inline float clampf(float x, float lo, float hi) {
    if (x < lo) return lo;
    if (x > hi) return hi;
    return x;
}

static inline int volfilter(int pos){
	if(pos > 100){
		return 100;
	}else if(pos < 0){
		return 0;
	}

	return pos;
}

// --- [Logic Layer] 비즈니스 로직 분리 ---
// 로터리 1이 움직였을 때 호출됨
static void Apply_Rotary1_Change(int8_t dir) {
    // 1. 위치값 단순 기록 (디버깅용)
    g_enc_pos[0] += dir;

    UI_OnEncoderDelta((int)dir);

    // 2. 실제 기능 (Q Factor 조절)
//    float q = g_lpf_Q + (float)dir * Q_STEP;
//    g_lpf_Q = clampf(q, Q_MIN, Q_MAX);
//    g_lpf_dirty = 1; // 오디오 태스크에 변경 알림
//
//    printf("[R1] Q-Factor: %.2f (Pos: %ld)\r\n", g_lpf_Q, g_enc_pos[0]);
}

// 로터리 2가 움직였을 때 호출됨
static void Apply_Rotary2_Change(int8_t dir) {
    // 1. 위치값 단순 기록
	g_enc_pos[1] = volfilter(g_enc_pos[1] + dir);
	UI_OnChangeVolume((int)dir);
    // 2. 실제 기능 (Cutoff Frequency 조절)
//    float fc = g_lpf_FC + (float)dir * FC_STEP;
//    g_lpf_FC = clampf(fc, FC_MIN, FC_MAX);
//    g_lpf_dirty = 1;
//
    printf("[R2] Pos: %ld \r\n", g_enc_pos[1]);
}

// --- [Driver Layer] 하드웨어 폴링 태스크 ---
void InputTask(void *arg) {
    // [초기화] 현재 타이머 값 읽기
    // TIM4는 16비트, TIM5는 32비트지만 uint32_t로 통일해서 받아도 됨
    uint32_t last_cnt1 = __HAL_TIM_GET_COUNTER(&htim4);
    uint32_t last_cnt2 = __HAL_TIM_GET_COUNTER(&htim5);

    // 스텝 누적 변수 (나머지 처리용)
    int16_t acc1 = 0;
    int16_t acc2 = 0;

    g_enc_pos[1] = 50;

    for (;;) {
        // 1. 현재 카운터 값 읽기
        uint32_t curr_cnt1 = __HAL_TIM_GET_COUNTER(&htim4);
        uint32_t curr_cnt2 = __HAL_TIM_GET_COUNTER(&htim5);

        // 2. 변화량 계산 (핵심: int16_t 캐스팅으로 오버플로우 자동 해결)
        // TIM4(16bit)가 0 -> 65535가 되어도 int16_t로 변환하면 -1이 됨
        int16_t diff1 = (int16_t)(curr_cnt1 - last_cnt1);
        int16_t diff2 = (int16_t)(curr_cnt2 - last_cnt2);

        // 3. 기준점 업데이트 (무조건 수행해야 함)
        last_cnt1 = curr_cnt1;
        last_cnt2 = curr_cnt2;

        // --- Rotary 1 처리 ---
        if (diff1 != 0) {
            acc1 -= diff1;
            // 설정된 스텝(예: 2칸) 이상 쌓이면 1칸 이동으로 간주
            if (abs(acc1) >= ENC_STEPS_PER_NOTCH) {
                int8_t dir = (acc1 > 0) ? 1 : -1;

                // [Logic 호출] 여기서 비즈니스 로직 함수만 딱 부름
                Apply_Rotary1_Change(dir);

                // 처리 후 누적값에서 해당 스텝만큼 뺌 (나머지는 유지하여 부드럽게)
                acc1 -= (dir * ENC_STEPS_PER_NOTCH);
                // 혹은 acc1 = 0; 으로 하면 딱딱 끊어지는 느낌
            }
        }

        // --- Rotary 2 처리 ---
        if (diff2 != 0) {
            acc2 -= diff2;
            if (abs(acc2) >= ENC_STEPS_PER_NOTCH) {
                int8_t dir = (acc2 > 0) ? 1 : -1;

                // [Logic 호출]
                Apply_Rotary2_Change(dir);

                acc2 -= (dir * ENC_STEPS_PER_NOTCH);
            }
        }

        // 10ms 대기 (너무 빠르면 CPU 낭비, 너무 느리면 반응성 저하)
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void RotaryTasks_Init(void) {
	BaseType_t ok = xTaskCreate(InputTask, "InputTask", 512, NULL,
			tskIDLE_PRIORITY + 2, NULL);
	configASSERT(ok == pdPASS);
}

//// --- [수정됨] ISR: 핀 상태를 즉시 읽어 전달 ---
//void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin) {
//	// --- Rotary 1 (ENC1) ---
//	if (GPIO_Pin == Rotary1_KEY_Pin) {
//		printf("ro1_key\n");
//		return;
//	}
//
//	// --- Rotary 2 (ENC2) ---
//	if (GPIO_Pin == Rotary2_KEY_Pin) {
//		printf("ro2_key\n");
//		return;
//	}
//}
