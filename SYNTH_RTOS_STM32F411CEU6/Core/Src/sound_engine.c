/*
 * sound_engine.c
 *
 *  Created on: Jan 9, 2026
 *      Author: kccistc
 */



#include "FreeRTOS.h"
#include "task.h"
#include "main.h"

#include <math.h>
#include "user_rtos.h"

#define OCTAVE_SHIFT  1

extern I2S_HandleTypeDef hi2s1;

double freq_list[] = {FREQ_C4, FREQ_D4, FREQ_E4, FREQ_F4, FREQ_G4, FREQ_A4, FREQ_B4};

// --- 변수 ---
typedef enum {
	ADSR_IDLE, ADSR_ATTACK, ADSR_DECAY, ADSR_SUSTAIN, ADSR_RELEASE
} ADSR_State_t;

typedef struct {
	// 설정값 (Time은 샘플 개수 단위, Level은 0.0~1.0)
	uint32_t attack_steps;   // Attack에 걸리는 시간 (샘플 수)
	uint32_t decay_steps;    // Decay에 걸리는 시간 (샘플 수)
	float sustain_level;  // Sustain 볼륨 (0.0 ~ 1.0)
	uint32_t release_steps;  // Release에 걸리는 시간 (샘플 수)

	// 내부 상태 변수
	ADSR_State_t state;
	float current_level;     // 현재 볼륨 (0.0 ~ 1.0)
	float step_val;          // 한 샘플당 변화량 (덧셈/뺄셈)
} ADSR_Control_t;

typedef enum {
	WAVE_SINE, WAVE_SAW, WAVE_SQUARE
} WaveType_t;

// --- 변수 ---
int16_t i2s_buffer[BUFFER_SIZE];

// 3가지 파형 테이블
int16_t sine_lut[LUT_SIZE];
int16_t saw_lut[LUT_SIZE];
int16_t square_lut[LUT_SIZE];

// [핵심] 현재 사용할 테이블을 가리키는 포인터 (기본값: 사인파)
int16_t *current_lut = sine_lut;

uint32_t phase_accumulator = 0;
uint32_t tuning_word = 0;
volatile float target_freq = 440.0f;
TaskHandle_t audioTaskHandle = NULL;


volatile float g_lpf_Q = 0.707f;
volatile float g_lpf_FC = 1500.f;
volatile uint8_t g_lpf_dirty = 1;



ADSR_Control_t adsr = { .attack_steps = 4410,    // 0.1s // 현재 전송 속도 44.1KHz
		.decay_steps = 4410,     // 0.1s
		.sustain_level = 0.5f,   // 50% volume
		.release_steps = 13230,  // 0.3s
		.state = ADSR_IDLE, .current_level = 0.0f, .step_val = 0.0f };

//ADSR_Control_t adsr = { .attack_steps = 0,    // 0.1s // 현재 전송 속도 44.1KHz
//		.decay_steps = 2205,     // 0.1s
//		.sustain_level = 0,   // 50% volume
//		.release_steps = 2205,  // 0.3s
//		.state = ADSR_IDLE, .current_level = 0.0f, .step_val = 0.0f };

void NoteOn(void) {
	adsr.state = ADSR_ATTACK;
	// 0.0에서 1.0까지 가는데 필요한 스텝 계산
	// 이미 소리가 나고 있는 중일 수도 있으므로 (1.0 - 현재) / steps
	adsr.step_val = (1.0f - adsr.current_level) / (float) adsr.attack_steps;
	// 차이를 앞으로 갈 스탭 수로 나눔 = 한 스탭 당 바뀌어야하는 값
}

void NoteOff(void) {
	adsr.state = ADSR_RELEASE;
	// 현재 레벨에서 0.0까지 가는데 필요한 스텝
	adsr.step_val = adsr.current_level / (float) adsr.release_steps;
}

void Init_All_LUTs(void) {
	int16_t amplitude = 7000; // 이게 최고 볼륭, 이론 상 최고 볼륨은 32,767

	for (int i = 0; i < LUT_SIZE; i++) {
		// 1. Sine Wave (기존과 동일)
		sine_lut[i] = (int16_t) (amplitude
				* sinf(2.0f * 3.141592f * (float) i / (float) LUT_SIZE));

		// 2. Sawtooth Wave (톱니파: -Amp ~ +Amp 선형 증가)
		// 공식: -Amp + (2 * Amp * i / Size)
		float saw_val = -amplitude + (2.0f * amplitude * i / (float) LUT_SIZE);
		saw_lut[i] = (int16_t) saw_val;

		// 3. Square Wave (사각파: 절반은 +Amp, 절반은 -Amp)
		if (i < LUT_SIZE / 2) {
			square_lut[i] = amplitude;
		} else {
			square_lut[i] = -amplitude;
		}
	}
}


void Calc_Wave_LUT(int16_t *buffer, int length)
{
    tuning_word = (uint32_t)((double)target_freq * 4294967296.0 / (double)SAMPLE_RATE);

    // ✅ 필터는 “한 번만” 초기화 (상태 유지)
    static Biquad lpf;
    static int inited = 0;


    if (!inited) {
        biquad_reset(&lpf);
        biquad_set_lpf(&lpf, (float)SAMPLE_RATE, g_lpf_FC, g_lpf_Q); // ✅ Fs는 SAMPLE_RATE로
        inited = 1;
    }

    if (g_lpf_dirty) {
        g_lpf_dirty = 0;
        biquad_set_lpf(&lpf, (float)SAMPLE_RATE, g_lpf_FC, g_lpf_Q);
    }
    for (int i = 0; i < length; i += 2) {

        // --- [1] ADSR 상태 머신 처리 ---
        switch (adsr.state) {
        case ADSR_IDLE:
            adsr.current_level = 0.0f;
            break;

        case ADSR_ATTACK:
            adsr.current_level += adsr.step_val;
            if (adsr.current_level >= 1.0f) {
                adsr.current_level = 1.0f;
                adsr.state = ADSR_DECAY;
                adsr.step_val = (1.0f - adsr.sustain_level) / (float)adsr.decay_steps;
            }
            break;

        case ADSR_DECAY:
            adsr.current_level -= adsr.step_val;
            if (adsr.current_level <= adsr.sustain_level) {
                adsr.current_level = adsr.sustain_level;
                adsr.state = ADSR_SUSTAIN;
                adsr.step_val = 0.0f;
            }
            break;

        case ADSR_SUSTAIN:
            break;

        case ADSR_RELEASE:
            adsr.current_level -= adsr.step_val;
            if (adsr.current_level <= 0.0f) {
                adsr.current_level = 0.0f;
                adsr.state = ADSR_IDLE;
            }
            break;
        }

        // --- [2] 무음이면 출력 0 ---
        if (adsr.current_level <= 0.0001f) {
            buffer[i] = 0;
            buffer[i + 1] = 0;
            phase_accumulator += tuning_word;
            continue;
        }

        // --- [3] 파형 생성 + ADSR ---
        uint32_t index = phase_accumulator >> LUT_SHIFT;
        int16_t raw_val = current_lut[index];
        float s = (float)raw_val * adsr.current_level; // float로 유지

        // --- [4] ✅ IIR 필터 적용 ---
        float x = s / 32768.0f;
        float y = biquad_process(&lpf, x);

        float out_f = y * 32767.0f;
        if (out_f >  32767.0f) out_f =  32767.0f;
        if (out_f < -32768.0f) out_f = -32768.0f;

        int16_t out = (int16_t)out_f;

        buffer[i]     = out;
        buffer[i + 1] = out;

        phase_accumulator += tuning_word;
    }
}
void StartAudioTask(void *argument) {
	audioTaskHandle = xTaskGetCurrentTaskHandle();

	Init_All_LUTs(); // 이름 변경됨

	Calc_Wave_LUT(&i2s_buffer[0], BUFFER_SIZE); // 이름 변경됨

	HAL_I2S_Transmit_DMA(&hi2s1, (uint16_t*) i2s_buffer, BUFFER_SIZE);

	uint32_t ulNotificationValue;

	// 초기화 완료

	for (;;) {
		xTaskNotifyWait(0, 0xFFFFFFFF, &ulNotificationValue, portMAX_DELAY);

		if ((ulNotificationValue & 0x01) != 0) {
			Calc_Wave_LUT(&i2s_buffer[0], BUFFER_SIZE / 2); // 이름 변경됨
		}

		if ((ulNotificationValue & 0x02) != 0) {
			Calc_Wave_LUT(&i2s_buffer[BUFFER_SIZE / 2], BUFFER_SIZE / 2); // 이름 변경됨
		}
	}
}

void HAL_I2S_TxHalfCpltCallback(I2S_HandleTypeDef *hi2s) {
	BaseType_t xHigherPriorityTaskWoken = pdFALSE;
	if (audioTaskHandle != NULL) {
		// 태스크에 알림 전송 (Bit 0 설정)
		xTaskNotifyFromISR(audioTaskHandle, 0x01, eSetBits,
				&xHigherPriorityTaskWoken);
		portYIELD_FROM_ISR(xHigherPriorityTaskWoken); // 필요시 즉시 문맥 전환
	}
}

void HAL_I2S_TxCpltCallback(I2S_HandleTypeDef *hi2s) {
	BaseType_t xHigherPriorityTaskWoken = pdFALSE;
	if (audioTaskHandle != NULL) {
		// 태스크에 알림 전송 (Bit 1 설정)
		xTaskNotifyFromISR(audioTaskHandle, 0x02, eSetBits,
				&xHigherPriorityTaskWoken);
		portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
	}
}


void InitTasks(void) {
	xTaskCreate(StartAudioTask, "AudioTask", 256, NULL, 52, &audioTaskHandle);
}

void Test(void) {
//	current_lut = sine_lut;
//	current_lut = saw_lut;
//	current_lut = square_lut;
//	// 1. 도(C4) 누르기
//	target_freq = FREQ_C4;
//	NoteOn();
//	vTaskDelay(pdMS_TO_TICKS(1000));
//
//	// 2. 떼기
//	NoteOff();
//	vTaskDelay(pdMS_TO_TICKS(1000));
//
//	// 3. 미(E4) 누르기
//	target_freq = FREQ_E4;
//	NoteOn();
//	vTaskDelay(pdMS_TO_TICKS(1000));
//
//	// 4. 떼기
//	NoteOff();
//	vTaskDelay(pdMS_TO_TICKS(1000));
//
//	// 5. 솔(G4)
//	target_freq = FREQ_G4;
//	NoteOn();
//	vTaskDelay(pdMS_TO_TICKS(1000));
//	NoteOff();
	vTaskDelay(pdMS_TO_TICKS(1000));
}
