/*
 * btn.c
 *
 *  Created on: Jan 9, 2026
 *      Author: kccistc
 */

#include "FreeRTOS.h"
#include "task.h"
#include "main.h"

#include "semphr.h"
#include <stdint.h>
#include <stdio.h>

#include "user_rtos.h"

#define C1_GPIO_Port  GPIOB
#define C1_Pin        GPIO_PIN_13
#define C2_GPIO_Port  GPIOA
#define C2_Pin        GPIO_PIN_10
#define C3_GPIO_Port  GPIOA
#define C3_Pin        GPIO_PIN_9
#define C4_GPIO_Port  GPIOA
#define C4_Pin        GPIO_PIN_8

#define R1_GPIO_Port  GPIOB
#define R1_Pin        GPIO_PIN_12
#define R2_GPIO_Port  GPIOB
#define R2_Pin        GPIO_PIN_4
#define R3_GPIO_Port  GPIOB
#define R3_Pin        GPIO_PIN_1
#define R4_GPIO_Port  GPIOB
#define R4_Pin        GPIO_PIN_0

static GPIO_TypeDef *const COL_PORT[4] = { C1_GPIO_Port, C2_GPIO_Port,
		C3_GPIO_Port, C4_GPIO_Port };
static const uint16_t COL_PIN[4] = { C1_Pin, C2_Pin, C3_Pin, C4_Pin };

static GPIO_TypeDef *const ROW_PORT[4] = { R1_GPIO_Port, R2_GPIO_Port,
		R3_GPIO_Port, R4_GPIO_Port };
static const uint16_t ROW_PIN[4] = { R1_Pin, R2_Pin, R3_Pin, R4_Pin };

/* ====== RTOS handles ====== */
static SemaphoreHandle_t printfMutex = NULL;

volatile uint8_t KEY;

typedef enum {
	EV_KEY_DOWN = 0, EV_KEY_UP = 1,
} InputEventType;

typedef struct {
	InputEventType type;
	uint8_t key;       // 0~15
} InputEvent;

static const char* ev_name(InputEventType t) {
	return (t == EV_KEY_DOWN) ? "DOWN" : "UP";
}

static uint16_t matrix_scan_raw(void) {
	uint16_t mask = 0;


	/* 모든 컬럼 HIGH */
	for (int c = 0; c < 4; c++) {
		HAL_GPIO_WritePin(COL_PORT[c], COL_PIN[c], GPIO_PIN_SET);
	}

	for (int c = 0; c < 4; c++) {
		/* 현재 컬럼만 LOW */
		HAL_GPIO_WritePin(COL_PORT[c], COL_PIN[c], GPIO_PIN_RESET);

		/* 아주 짧은 settle (RTOS 딜레이 말고 NOP로 충분) */
		for (volatile int i = 0; i < 200; i++) {
			__NOP();
		}

		/* 로우 읽기: 눌리면 LOW (Row input pull-up 가정) */
		for (int r = 0; r < 4; r++) {
			GPIO_PinState s = HAL_GPIO_ReadPin(ROW_PORT[r], ROW_PIN[r]);
			if (s == GPIO_PIN_RESET) {
				int idx = r * 4 + c;
				mask |= (uint16_t) (1u << idx);
			}
		}

		/* 컬럼 다시 HIGH */
		HAL_GPIO_WritePin(COL_PORT[c], COL_PIN[c], GPIO_PIN_SET);
	}

	return mask;
}

/* ====== debounce (integrator 방식) ====== */
#define DEB_MAX 3
static uint8_t integ[16] = { 0 };
static uint16_t stable_mask = 0;   // 디바운스 후 안정 상태

/* raw -> stable 업데이트, "눌림 엣지(0->1)"만 events_mask에 세팅 */
static void debounce_update(uint16_t raw, uint16_t *down_edges,
		uint16_t *up_edges) {
	uint16_t down = 0, up = 0;

	for (int i = 0; i < 16; i++) {
		uint8_t raw_pressed = (raw >> i) & 1u;
		uint8_t st_pressed = (stable_mask >> i) & 1u;

		if (raw_pressed) {
			if (integ[i] < DEB_MAX)
				integ[i]++;
		} else {
			if (integ[i] > 0)
				integ[i]--;
		}

		if (!st_pressed && integ[i] == DEB_MAX) {
			stable_mask |= (uint16_t) (1u << i);
			down |= (uint16_t) (1u << i);  // pressed edge
		}
		if (st_pressed && integ[i] == 0) {
			stable_mask &= (uint16_t) ~(1u << i); // release (원하면 여기서 release 이벤트도 만들 수 있음)
			up |= (uint16_t) (1u << i);
		}
	}

	*down_edges = down;
	*up_edges = up;
}

/* ====== tasks ====== */

static void print_event(const InputEvent *e) {
	// 사람이 보기 좋게 1~16로 표시하려면 key+1

	//static uint8_t count_arr[7];

	if (printfMutex)
		xSemaphoreTake(printfMutex, portMAX_DELAY);

	KEY = e->key;
	if (e->key == 7) {
		current_lut = saw_lut;
	} else if (e->key == 8) {
		current_lut = square_lut;
	} else if (e->key == 9) {
		current_lut = sine_lut;
	}

	if (e->type == EV_KEY_DOWN && e->key < 7) {
		target_freq = freq_list[e->key];
		NoteOn();
	} else if (e->type == EV_KEY_UP && e->key < 7) {
		NoteOff();
	}
	printf("[EV] key=%u type=%s\r\n", (unsigned) (e->key + 1),
			ev_name(e->type));
	if (printfMutex)
		xSemaphoreGive(printfMutex);
}

static void KeyScanTask(void *arg) {
	(void) arg;
	const TickType_t period = pdMS_TO_TICKS(5);

	for (;;) {
		uint16_t raw = matrix_scan_raw();

		uint16_t downs = 0, ups = 0;
		debounce_update(raw, &downs, &ups);

		// DOWN 이벤트들 출력
		while (downs) {
			int idx = __builtin_ctz(downs);
			downs &= (uint16_t) (downs - 1);

			InputEvent e = { .type = EV_KEY_DOWN, .key = (uint8_t) idx };
			print_event(&e);
		}

		// UP 이벤트들 출력
		while (ups) {
			int idx = __builtin_ctz(ups);
			ups &= (uint16_t) (ups - 1);

			InputEvent e = { .type = EV_KEY_UP, .key = (uint8_t) idx };
			print_event(&e);
		}

		vTaskDelay(period);
	}
}

/* ====== init: StartDefaultTask에서 한 번만 호출 ====== */
void KeypadTasks_Init(void) {
	if (printfMutex == NULL) {
		printfMutex = xSemaphoreCreateMutex();
	}
	static uint8_t started = 0;
	if (started)
		return;
	started = 1;

	stable_mask = 0;
	for (int i = 0; i < 16; i++)
		integ[i] = 0;

	BaseType_t ok = xTaskCreate(KeyScanTask, "KeyScan", 256,
	NULL,
	tskIDLE_PRIORITY + 2,
	NULL);
	configASSERT(ok == pdPASS);
}
