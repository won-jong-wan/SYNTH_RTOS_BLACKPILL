#include "main.h"
#include <unistd.h>

extern UART_HandleTypeDef huart3;

int _write(int file, char *ptr, int len)
{
    (void)file;
    HAL_UART_Transmit(&huart3, (uint8_t*)ptr, (uint16_t)len, 100);
    return len;
}
