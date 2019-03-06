/*  Example Configuration for the steppermotor driver DRV8825
*/
#include <math.h>
#include "motor_control_task.h"
#include "position_queue.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "driver/gpio.h"

/* STEPPER DEFINITIONS */
#define STEPPER_COUNT           2000 /* steps needed to open blinds from 0-100% */
#define STEPPER_DELAY           20   /* Minimum delay required between steps */
#define GPIO_STEPPER_ENABLE     21    /* enable stepper driver */
#define GPIO_STEPPER_DIR        22    /* set direction for the stepper */
#define GPIO_STEPPER_STEP       23    /* Pin that triggers steps */
#define GPIO_STEPPER_PINS  ((1ULL<<GPIO_STEPPER_ENABLE) \
    | (1ULL<<GPIO_STEPPER_DIR) | (1ULL<<GPIO_STEPPER_STEP))

static const char *TAG = "MOTOR_CONTROL_TASK";
TaskHandle_t motor_control_handle = NULL;

static esp_err_t set_new_position(uint8_t new_position)
{
    nvs_handle task_nvs_handle;
    esp_err_t error_code;

    /*  open NVS flash */
    ESP_LOGI(TAG, "Opening NVS handle");
    error_code = nvs_open("position", NVS_READWRITE, &task_nvs_handle);
    if (error_code != ESP_OK) return error_code;
    ESP_LOGI(TAG, "NVS storage partition opened");

    /*  Read old position from NVS
    *   old position defaults to 0 if not set in NVS */
    uint8_t old_position = 0;
    error_code = nvs_get_u8(task_nvs_handle, "old_position", &old_position);
    /*  value can´t be found on first run -> ESP_ERR_NVS_NOT_FOUND */
    if (error_code == ESP_OK)
    {
        ESP_LOGI(TAG, "NVS old position was %d", old_position);
    }else if (error_code == ESP_ERR_NVS_NOT_FOUND)
    {
        ESP_LOGI(TAG, "Value it not initialized yet");
    }else{
        return error_code;
    }

    /* Calculate how many Steps are needed */
    int32_t movement = round((new_position - old_position) * (STEPPER_COUNT/100));
    /* set direction for the stepper */
    if (movement > 0)
    {
        gpio_set_level(GPIO_STEPPER_DIR, 1);
    }else if (movement < 0){
        gpio_set_level(GPIO_STEPPER_DIR, 0);
        movement*=-1;
    }

    /*  Move to new position */
    for (int32_t steps = 0; steps < movement; ++steps )
    {
        gpio_set_level(GPIO_STEPPER_STEP, 1);
        vTaskDelay(STEPPER_DELAY / portTICK_PERIOD_MS);
        gpio_set_level(GPIO_STEPPER_STEP, 0);
        vTaskDelay(STEPPER_DELAY / portTICK_PERIOD_MS);
    }

    /*  Write new position to NVS */
    ESP_LOGI(TAG, "save new position in Non Volatile Storage");
    error_code = nvs_set_u8(task_nvs_handle, "old_position", new_position);
    if (error_code != ESP_OK) return error_code;

    /*  Commit written value.
    *   After setting any values, nvs_commit() must be called to ensure changes are written
    *   to flash storage. */
    ESP_LOGI(TAG, "Commit changes to Non Volatile Storage");
    error_code = nvs_commit(task_nvs_handle);
    if (error_code != ESP_OK) return error_code;

    /*  Close NVS */
    ESP_LOGI(TAG, "Close Non Volatile Storage");
    nvs_close(task_nvs_handle);
    return ESP_OK;
}

/**@brief Task that reads the new position from the queue an adjusts the blind
 * position
 */
static void motor_control_task(void *arg)
{
    ESP_LOGI(TAG, "Start Motor Control Task");
    /* activate stepper driver so it does not move */
    gpio_set_level(GPIO_STEPPER_ENABLE, 1);

    uint8_t new_position;

    for(;;)
    {
        /*  if a message could be received from the queue within 10 sek */
        if (xQueueReceive(position_queue, &new_position,
            10000/portTICK_RATE_MS) == pdTRUE)
        {
            ESP_LOGI(TAG, "Received a new value from the queue: %d",
                (int)new_position);
            esp_err_t error_code = set_new_position(new_position);
            if (error_code != ESP_OK)
            {
                ESP_LOGI(TAG, "ERROR: %d", error_code);
            }
        }

        /*  pause the task for 1sek, so a other task could be started if required */
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

/**@brief Function for initializing the Output Pins
 * for the stepper driver
 */
static void task_gpio_init(void)
{
    gpio_config_t io_conf;

    /* SET GPIO CONFIG FOR STEPPER PINS */
    /*  disable interrupt */
    io_conf.intr_type = GPIO_PIN_INTR_DISABLE;
    /*  set as output mode */
    io_conf.mode = GPIO_MODE_OUTPUT;
    /*  bit mask of the pins */
    io_conf.pin_bit_mask = GPIO_STEPPER_PINS;
    /*  disable pull-down mode */
    io_conf.pull_down_en = 0;
    /*  disable pull-up mode */
    io_conf.pull_up_en = 0;
    /*  configure GPIO with the given settings */
    gpio_config(&io_conf);
}

/**@brief Function for initializing the Task of the motor control
 */
esp_err_t motor_control_task_init(void)
{
    xTaskCreate(
        motor_control_task,     /* Task function */
        "MOTOR_CONTROL",        /* Name of task */
        2048,                   /* Stack size of task */
        NULL,                   /* parameter of the task */
        4,                      /* priority of the task (high is important) */
        &motor_control_handle);   /* Task handle to keep track of created Task */

    task_gpio_init();
    return ESP_OK;
}
