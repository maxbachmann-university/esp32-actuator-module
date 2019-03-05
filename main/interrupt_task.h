#ifndef __INTERRUPT_TASK__
#define __INTERRUPT_TASK__

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "esp_err.h"

/*  inform c++ compilers that the function should be compiled in C Style */
#ifdef __cplusplus
extern "C" {
#endif

esp_err_t interrupt_task_init(void);

#ifdef __cplusplus
}
#endif

#endif /* __INTERRUPT_TASK__ */
