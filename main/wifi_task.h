#ifndef __WIFI_TASK__
#define __WIFI_TASK__

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "esp_err.h"

/*  inform c++ compilers that the function should be compiled in C Style */
#ifdef __cplusplus
extern "C" {
#endif

esp_err_t wifi_task_init(void);

#ifdef __cplusplus
}
#endif

#endif /* __WIFI_TASK__ */
