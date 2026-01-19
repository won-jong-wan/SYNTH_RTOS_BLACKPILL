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
#include <stdio.h>
#include "ui.h"

// --- 설정값 정의 ---
// Rotary 1 (Q Factor)
#define Q_MIN 0.50f
#define Q_MAX 8.00f
#define Q_STEP 0.10f

// Rotary 2 (Cutoff Frequency)
#define FC_MIN   50.0f
#define FC_MAX   8000.0f
#define FC_STEP  50.0f

#define OCTAVE_SHIFT  1
#define SOUND_MAX 32767.0f
#define SAMPLES_PER_MS  44  // 44.1kHz 기준 약 1ms

extern I2S_HandleTypeDef hi2s1;

double freq_list[] = { FREQ_C4, FREQ_D4, FREQ_E4, FREQ_F4, FREQ_G4, FREQ_A4,
FREQ_B4 };

volatile int16_t g_vis_buffer[VIS_BUF_SIZE] = { 0 };

uint8_t count_arr[7] = { 0 };

// --- 변수 ---
typedef enum {
	ADSR_IDLE, ADSR_ATTACK, ADSR_DECAY, ADSR_SUSTAIN, ADSR_RELEASE
} ADSR_State_t;

typedef struct {
	float freq;
	uint8_t count;
	uint32_t phase_accumulator;
	uint32_t tuning_word;
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

volatile float enc_val;

ADSR_Control_t basic_adsr = { .freq = FREQ_C4, .tuning_word = 0.f, .count = 0,
		.phase_accumulator = 0, .attack_steps = 4410, // 0.1s // 현재 전송 속도 44.1KHz
		.decay_steps = 4410,     // 0.1s
		.sustain_level = 0.5f,   // 50% volume
		.release_steps = 13230,  // 0.3s
		.state = ADSR_IDLE, .current_level = 0.0f, .step_val = 0.0f };

//ADSR_Control_t adsr = { .attack_steps = 0,    // 0.1s // 현재 전송 속도 44.1KHz
//		.decay_steps = 2205,     // 0.1s
//		.sustain_level = 0,   // 50% volume
//		.release_steps = 2205,  // 0.3s
//		.state = ADSR_IDLE, .current_level = 0.0f, .step_val = 0.0f };

ADSR_Control_t adsrs[MAX_VOICES];

void NoteOn(void) {
	//adsr.state = ADSR_ATTACK;
	// 0.0에서 1.0까지 가는데 필요한 스텝 계산
	// 이미 소리가 나고 있는 중일 수도 있으므로 (1.0 - 현재) / steps

	//adsr.step_val = (1.0f - adsr.current_level) / (float) adsr.attack_steps;
	// 차이를 앞으로 갈 스탭 수로 나눔 = 한 스탭 당 바뀌어야하는 값

	int8_t new_voice_idx = 0;
	int8_t min_count = 127;

	static int8_t count = 0;

	for (int i = 0; i < MAX_VOICES; i++) {
		if (adsrs[i].state == ADSR_IDLE) {
			new_voice_idx = i;
			break;
		}
		if (adsrs[i].count < min_count) {
			min_count = adsrs[i].count;
			new_voice_idx = i;
		}

	}
//    printf("New voice idx %d\n", new_voice_idx);

	// [수정] 옥타브 계산 로직 추가
	float base_freq = target_freq; // target_freq는 보통 4옥타브 기준 (예: C4 = 261.63Hz)
	float final_freq = base_freq;

	int shift = (int) g_ui_oct - 4; // 4옥타브가 기준(0)

	if (shift > 0) {
		// 옥타브 올림 (주파수 2배씩)
		for (int k = 0; k < shift; k++)
			final_freq *= 2.0f;
	} else if (shift < 0) {
		// 옥타브 내림 (주파수 절반씩)
		for (int k = 0; k < -shift; k++)
			final_freq *= 0.5f;
	}

	// 1. Attack (UI값 1당 약 5ms 정도로 가정)
	adsrs[new_voice_idx].attack_steps = g_ui_adsr.attack_steps
			* (5 * SAMPLES_PER_MS);

	// 2. Decay
	adsrs[new_voice_idx].decay_steps = g_ui_adsr.decay_steps
			* (5 * SAMPLES_PER_MS);

	// 3. Sustain (UI 0~100 -> 0.0 ~ 1.0 실수로 변환)
	adsrs[new_voice_idx].sustain_level = (float) g_ui_adsr.sustain_level
			/ 100.0f;

	// 4. Release
	adsrs[new_voice_idx].release_steps = g_ui_adsr.release_steps
			* (5 * SAMPLES_PER_MS);

	adsrs[new_voice_idx].freq = final_freq;
	adsrs[new_voice_idx].count = ++count;
	adsrs[new_voice_idx].state = ADSR_ATTACK;
	// [중요] Step Value 재계산 (Attack 시간에 맞춰서)
	if (adsrs[new_voice_idx].attack_steps > 0) {
		adsrs[new_voice_idx].step_val = 1.0f
				/ (float) adsrs[new_voice_idx].attack_steps;
	} else {
		adsrs[new_voice_idx].step_val = 1.0f; // 즉시 최대 볼륨
	}

	adsrs[new_voice_idx].tuning_word = (uint32_t) ((double) final_freq
			* 4294967296.0 / (double) SAMPLE_RATE);

	// 어택 시작은 0부터
	adsrs[new_voice_idx].current_level = 0.0f;
//    printf("idle to attack\n");

	if (count >= 127) {
		int8_t min = 127;

		// 1) 활성 voice의 최소 count 찾기
		for (int i = 0; i < MAX_VOICES; i++) {
			if (adsrs[i].state != ADSR_IDLE && adsrs[i].count < min) {
				min = adsrs[i].count;
			}
		}
		// 활성 voice가 하나도 없으면 그냥 초기화
		if (min == 127) {
			count = 0;
		} else {
			int8_t offset = (int8_t) (min - 1);

			// 2) 모든 voice count를 동일 오프셋만큼 당김 (상대관계 유지)
			for (int i = 0; i < MAX_VOICES; i++) {
				if (adsrs[i].state != ADSR_IDLE) {
					adsrs[i].count -= offset;
				} else {
					adsrs[i].count = 0;
				}
			}
			// 3) 전역 count도 같은 기준으로 보정
			count -= offset;

		}
	}
	count_arr[KEY] = count;
}

void NoteOff(void) {

	for (int i = 0; i < MAX_VOICES; i++) {
		if (adsrs[i].count == count_arr[KEY]) {
			adsrs[i].state = ADSR_RELEASE;
			adsrs[i].step_val = adsrs[i].current_level
					/ (float) adsrs[i].release_steps;
			break;
		}
	}

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

float map_and_snap(float input_0_100, float min_val, float max_val,
		float step_val) {
	// 1. 입력값 범위 제한 (Clamping): 0~100을 벗어나는 경우 방지
	if (input_0_100 < 0.0f)
		input_0_100 = 0.0f;
	if (input_0_100 > 100.0f)
		input_0_100 = 100.0f;

	// 2. 선형 보간 (Linear Interpolation)
	// 비율(0.0 ~ 1.0) 계산 후 범위에 적용
	float raw_val = min_val + (input_0_100 / 100.0f) * (max_val - min_val);

	// 3. 스텝 단위로 반올림 (Quantization)
	// (현재값 - 최소값)을 스텝으로 나누어 반올림 한 뒤, 다시 스텝을 곱해 복원
	float snapped_val = min_val
			+ roundf((raw_val - min_val) / step_val) * step_val;

	// 4. 부동소수점 오차로 인해 max를 살짝 넘거나 min보다 작아지는 경우 방지
	if (snapped_val > max_val)
		snapped_val = max_val;
	if (snapped_val < min_val)
		snapped_val = min_val;

	return snapped_val;
}

void Calc_Wave_LUT(int16_t *buffer, int length) {
	//tuning_word = (uint32_t)((double)target_freq * 4294967296.0 / (double)SAMPLE_RATE);

	// ✅ 필터는 “한 번만” 초기화 (상태 유지)
	static Biquad lpf;
	static int inited = 0;

	g_lpf_FC = map_and_snap((float)g_ui_cutoff, FC_MIN, FC_MAX, FC_STEP);
	g_lpf_Q = map_and_snap((float)g_ui_reso, Q_MIN, Q_MAX, Q_STEP);

	g_lpf_dirty = 1;

	printf("%f %f\n", g_lpf_FC, g_lpf_Q);

	if (!inited) {
		biquad_reset(&lpf);
		biquad_set_lpf(&lpf, (float) SAMPLE_RATE, g_lpf_FC, g_lpf_Q); // ✅ Fs는 SAMPLE_RATE로
		inited = 1;
	}

	if (g_lpf_dirty) {
		g_lpf_dirty = 0;
		biquad_set_lpf(&lpf, (float) SAMPLE_RATE, g_lpf_FC, g_lpf_Q);
	}

	for (int i = 0; i < length; i += 2) {
		buffer[i] = 0;
		buffer[i + 1] = 0;
		float s = 0;
		for (int voice_idx = 0; voice_idx < MAX_VOICES; voice_idx++) {
			target_freq = adsrs[voice_idx].freq;
			tuning_word = adsrs[voice_idx].tuning_word; //(uint32_t)((double)target_freq * 4294967296.0 / (double)SAMPLE_RATE);

			// --- [1] ADSR 상태 머신 처리 ---
			switch (adsrs[voice_idx].state) {
			case ADSR_IDLE:
				adsrs[voice_idx].current_level = 0.0f;
				break;

			case ADSR_ATTACK:
				adsrs[voice_idx].current_level += adsrs[voice_idx].step_val;

				if (adsrs[voice_idx].current_level >= 1.0f) {
					adsrs[voice_idx].current_level = 1.0f;
					adsrs[voice_idx].state = ADSR_DECAY;
					adsrs[voice_idx].step_val = (1.0f
							- adsrs[voice_idx].sustain_level)
							/ (float) adsrs[voice_idx].decay_steps;
//					printf("attack to decay\n");
				}
				break;

			case ADSR_DECAY:
				adsrs[voice_idx].current_level -= adsrs[voice_idx].step_val;
				if (adsrs[voice_idx].current_level
						<= adsrs[voice_idx].sustain_level) {
					adsrs[voice_idx].current_level =
							adsrs[voice_idx].sustain_level;
					adsrs[voice_idx].state = ADSR_SUSTAIN;
					adsrs[voice_idx].step_val = 0.0f;
//				    printf("ADSR_DECAY to ADSR_SUSTAIN\n");
				}
				break;

			case ADSR_SUSTAIN:
				break;

			case ADSR_RELEASE:
				adsrs[voice_idx].current_level -= adsrs[voice_idx].step_val;
				if (adsrs[voice_idx].current_level <= 0.0f) {
					adsrs[voice_idx].current_level = 0.0f;
					adsrs[voice_idx].state = ADSR_IDLE;
//					printf("ADSR_RELEASE to idle\n");
				}
				break;
			}

			// --- [2] 무음이면 출력 0 ---
			if (adsrs[voice_idx].current_level <= 0.0001f) {

				adsrs[voice_idx].phase_accumulator += tuning_word;
				continue;
			}

			// --- [3] 파형 생성 + ADSR ---
			uint32_t index = adsrs[voice_idx].phase_accumulator >> LUT_SHIFT;
			adsrs[voice_idx].phase_accumulator += tuning_word;

			int16_t raw_val = current_lut[index];
			s += (float) raw_val * adsrs[voice_idx].current_level; // float로 유지

		}
		// --- [4] ✅ IIR 필터 적용 ---
		float x = s / 32768.0f;
		float y = biquad_process(&lpf, x);

		float out_f = y * enc_val;
		if (out_f > 32767.0f)
			out_f = 32767.0f;
		if (out_f < -32768.0f)
			out_f = -32768.0f;

		int16_t out = (int16_t) out_f / 2;

		buffer[i] += out;
		buffer[i + 1] += out;

	}
	int capture_len = (length / 2); // 스테레오니까 샘플 쌍의 개수
	if (capture_len > VIS_BUF_SIZE)
		capture_len = VIS_BUF_SIZE;

	for (int k = 0; k < capture_len; k++) {
		// buffer[2*k]는 Left 채널, buffer[2*k+1]은 Right 채널
		// 그냥 Left만 가져옵니다.
		g_vis_buffer[k] = buffer[2 * k];
	}

	static uint8_t frame_skip = 0;

	// 약 4번에 1번만 UI 업데이트 요청 (약 30~40 FPS 조절용)
	frame_skip++;
	if (frame_skip > 3) {
		frame_skip = 0;
		g_ui_dirty.wave_graph = 1; // "UI야, 그림 그려라!"
	}
}
void StartAudioTask(void *argument) {

	audioTaskHandle = xTaskGetCurrentTaskHandle();

	for (int i = 0; i < MAX_VOICES; i++) {
		adsrs[i] = basic_adsr;
	}

	Init_All_LUTs();

	Calc_Wave_LUT(&i2s_buffer[0], BUFFER_SIZE);

	HAL_I2S_Transmit_DMA(&hi2s1, (uint16_t*) i2s_buffer, BUFFER_SIZE);

	uint32_t ulNotificationValue;

	// 초기화 완료

	for (;;) {
		xTaskNotifyWait(0, 0xFFFFFFFF, &ulNotificationValue, portMAX_DELAY);

		enc_val = (SOUND_MAX / 100.0f) * (float) g_ui_vol;

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
