#include <math.h>
#include "interrupt_task.h"
#include "motor_control_task.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "driver/gpio.h"

/*  END STOP DEFINITIONS */
#define GPIO_HIGH_END_STOP      4   /* End stop for 100% */
#define GPIO_LOW_END_STOP       5   /* End stop for 0% */
#define GPIO_END_STOPS  ((1ULL<<GPIO_HIGH_END_STOP) | (1ULL<<GPIO_LOW_END_STOP))
#define ESP_INTR_FLAG_DEFAULT 0

static const char *TAG = "INTERRUPT_TASK";
/* queue that handles handles GPIO events from isr */
static xQueueHandle gpio_evt_queue = NULL;

static esp_err_t set_end_stop_position(uint32_t io_num)
{
    nvs_handle task_nvs_handle;
    esp_err_t error_code;
    uint8_t new_position;

    /*  open NVS flash */
    ESP_LOGI(TAG, "Opening NVS handle");
    error_code = nvs_open("storage", NVS_READWRITE, &task_nvs_handle);
    if (error_code != ESP_OK) return error_code;

    /* Move the blinds to the new position */
    if (io_num == GPIO_HIGH_END_STOP)
    {
        new_position = 100;
    } else {
        new_position = 0;
    }

    /*  Write new position to NVS */
    error_code = nvs_set_u8(task_nvs_handle, "new_position", new_position);
    if (error_code != ESP_OK) return error_code;

    /*  Commit written value.
    *   After setting any values, nvs_commit() must be called to ensure changes are written
    *   to flash storage. */
    error_code = nvs_commit(task_nvs_handle);
    if (error_code != ESP_OK) return error_code;

    /*  Close NVS */
    nvs_close(task_nvs_handle);
    return ESP_OK;
}

static void IRAM_ATTR gpio_isr_handler(void* arg)
{
    uint32_t gpio_num = (uint32_t) arg;
    xQueueSendFromISR(gpio_evt_queue, &gpio_num, NULL);
}

static void gpio_task(void* arg)
{
    uint32_t io_num;
    for(;;) {
        if(xQueueReceive(gpio_evt_queue, &io_num, portMAX_DELAY)) {
            /*  when the interrupt is caused by a End Stop */
            if (io_num == GPIO_HIGH_END_STOP || io_num == GPIO_LOW_END_STOP)
            {
                ESP_LOGI(TAG, "End Stop reached: Correcting the current position");
                /*  delete motor control task, so nothing gets destroyed */
                vTaskDelete(motor_control_handle);

                /*  set the position to the end stop 
                *   if it fails it retries each second, since it will not work
                *   properly with a false current position */
                while (set_end_stop_position(io_num) != ESP_OK)
                {
                    ESP_LOGI(TAG, "Failed to set the right current position");
                    vTaskDelay(1000 / portTICK_PERIOD_MS);
                }

                /*  create motor control task again */
                motor_control_task_init();
            }
        }
    }
}

/**@brief Function for initializing the used GPIO Pins
 */
esp_err_t interrupt_task_init(void)
{
    gpio_config_t io_conf;

    /* SET GPIO CONFIG FOR END STOPS */
    /*  interrupt of rising edge */
    io_conf.intr_type = GPIO_PIN_INTR_POSEDGE;
    /*  bit mask of the pins */
    io_conf.pin_bit_mask = GPIO_END_STOPS;
    /*  set as input mode */ 
    io_conf.mode = GPIO_MODE_INPUT;
    /*  enable pull-up mode */
    io_conf.pull_up_en = 1;
    /*  configure GPIO with the given settings */
    gpio_config(&io_conf);

    /*  create a queue to handle gpio event from isr */
    gpio_evt_queue = xQueueCreate(10, sizeof(uint32_t));
    /*  start gpio task */
    xTaskCreate(
        gpio_task,      /* Task function */
        "gpio_task",    /* Name of task */
        2048,           /* Stack size of task */
        NULL,           /* parameter of the task */
        10,             /* priority of the task (high is important) */
        NULL);          /* Task handle to keep track of created Task */

    /*  install gpio isr service */
    gpio_install_isr_service(ESP_INTR_FLAG_DEFAULT);
    /*  hook isr handlers for specific end stops */
    gpio_isr_handler_add(GPIO_HIGH_END_STOP, gpio_isr_handler, (void*) GPIO_HIGH_END_STOP);
    gpio_isr_handler_add(GPIO_LOW_END_STOP, gpio_isr_handler, (void*) GPIO_LOW_END_STOP);
    return ESP_OK;
}
