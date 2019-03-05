#ifndef __POSITION_QUEUE_TASK__
#define __POSITION_QUEUE_TASK__

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

/*  Make position queue extern so tasks including the file can access it*/
extern xQueueHandle position_queue;

esp_err_t position_queue_init(void);

#ifdef __cplusplus
}
#endif

#endif /* __POSITION_QUEUE_TASK__ */
