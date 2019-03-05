#ifndef __NVS_FLASH_INITIALIZE_TASK__
#define __NVS_FLASH_INITIALIZE_TASK__

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "esp_err.h"

/*  inform c++ compilers that the function should be compiled in C Style */
#ifdef __cplusplus
extern "C" {
#endif

esp_err_t nvs_flash_initialize(void);

#ifdef __cplusplus
}
#endif

#endif /* __NVS_FLASH_INITIALIZE_TASK__ */
