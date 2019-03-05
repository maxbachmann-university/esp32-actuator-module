#ifndef __MOTOR_CONTROL_TASK__
#define __MOTOR_CONTROL_TASK__

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "esp_err.h"

/*  inform c++ compilers that the function should be compiled in C Style */
#ifdef __cplusplus
extern "C" {
#endif

/*  Make position queue extern so the task can be killed when a End Stop 
*   causes an interrupt */
extern TaskHandle_t motor_control_handle;

esp_err_t motor_control_task_init(void);

#ifdef __cplusplus
}
#endif

#endif /* __MOTOR_CONTROL_TASK__ */
