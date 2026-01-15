/*
 * user_rtos.h
 *
 *  Created on: Jan 9, 2026
 *      Author: kccistc
 */

#ifndef INC_USER_RTOS_H_
#define INC_USER_RTOS_H_
#include "main.h"

// --- 설정 ---
#define SAMPLE_RATE   44100
#define BUFFER_SIZE   4096 // 사실 상 2048
#define LUT_SIZE      1024
#define LUT_SHIFT     (32 - 10)

#define MAX_VOICES    3

// 주파수 설정
#define FREQ_C4       261.63f
#define FREQ_D4       293.66f
#define FREQ_E4       329.63f
#define FREQ_F4       349.23f
#define FREQ_G4       392.00f
#define FREQ_A4       440.00f
#define FREQ_B4       493.88f

extern double freq_list[];

extern volatile float target_freq;

extern int16_t *current_lut;
extern int16_t sine_lut[LUT_SIZE];
extern int16_t saw_lut[LUT_SIZE];
extern int16_t square_lut[LUT_SIZE];

//iir
#define Q_MIN  0.50f
#define Q_MAX  8.00f
typedef struct {
	// normalized coefficients (a0 == 1)
	float b0, b1, b2;
	float a1, a2;

	// state (Direct Form I)
	float x1, x2;
	float y1, y2;
} Biquad;

void biquad_reset(Biquad *q);
void biquad_set_lpf(Biquad *q, float Fs, float Fc, float Q);
float biquad_process(Biquad *q, float x);
extern volatile uint8_t g_lpf_dirty;   // 파라미터 바뀜 플래그
extern volatile uint8_t KEY;

extern volatile float g_lpf_Q;
extern volatile float g_lpf_FC;

typedef enum {
	EVT_ENC_AB = 0, EVT_BTN_EDGE = 1,
} evt_type_t;

extern TIM_HandleTypeDef htim4;
extern TIM_HandleTypeDef htim5;

extern void RotaryTasks_Init(void);
extern void InitTasks(void);
extern void Test(void);
extern void KeypadTasks_Init(void);
extern void NoteOn(void);
extern void NoteOff(void);

// ui
extern volatile int32_t g_enc_pos[2];

#endif /* INC_USER_RTOS_H_ */
