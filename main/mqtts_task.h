#ifndef __MQTTS_TASK__
#define __MQTTS_TASK__

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "esp_err.h"

/*  inform c++ compilers that the function should be compiled in C Style */
#ifdef __cplusplus
extern "C" {
#endif

esp_err_t mqtts_task_init(void);

#ifdef __cplusplus
}
#endif

#endif /* __MQTTS_TASK__ */
