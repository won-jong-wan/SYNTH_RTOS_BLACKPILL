/*
 * hal_freertos_tick.c
 *
 *  Created on: Jan 14, 2026
 *      Author: KCCISTC
 */


#include "stm32f4xx_hal.h"
#include "FreeRTOS.h"
#include "task.h"

uint32_t HAL_GetTick(void)
{
    // 스케줄러 시작 전에는 기존 uwTick 사용
    if (xTaskGetSchedulerState() == taskSCHEDULER_NOT_STARTED) {
        extern __IO uint32_t uwTick;
        return uwTick;
    }
    // 시작 후에는 FreeRTOS tick 사용
    return (uint32_t)xTaskGetTickCount();
}

void HAL_Delay(uint32_t Delay)
{
    if (xTaskGetSchedulerState() == taskSCHEDULER_NOT_STARTED) {
        // 스케줄러 전에는 기존 방식
        uint32_t tickstart = HAL_GetTick();
        while ((HAL_GetTick() - tickstart) < Delay) {}
        return;
    }
    vTaskDelay(pdMS_TO_TICKS(Delay));
}
