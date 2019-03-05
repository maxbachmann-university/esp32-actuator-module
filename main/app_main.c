#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "wifi_task.h"
#include "mqtts_task.h"
#include "motor_control_task.h"
#include "interrupt_task.h"
#include "position_queue.h"
#include "nvs_flash_initialize.h"

#include "esp_system.h"
#include "esp_event_loop.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"

#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"

#include "esp_log.h"

#include "rom/ets_sys.h"

static const char *TAG = "MOTOR_CONTROL_MAIN";

/***************************************************************************************************
 * @section Main
 **************************************************************************************************/

void app_main()
{
    ESP_LOGI(TAG, "[APP] Startup..");
    ESP_LOGI(TAG, "[APP] Free memory: %d bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "[APP] IDF version: %s", esp_get_idf_version());

    /*  set log levels */
    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set("MQTT_CLIENT", ESP_LOG_VERBOSE);
    esp_log_level_set("TRANSPORT_TCP", ESP_LOG_VERBOSE);
    esp_log_level_set("TRANSPORT_SSL", ESP_LOG_VERBOSE);
    esp_log_level_set("TRANSPORT", ESP_LOG_VERBOSE);
    esp_log_level_set("OUTBOX", ESP_LOG_VERBOSE);

    /*  initialize the nvs flash */
    nvs_flash_initialize();

    /*  create Queue for communication between the mqtt and motor control tasks */
    position_queue_init();

    /*  start Wifi task (runs on core 0) */
    wifi_task_init();

    /*  start MQTT task (runs on core 0) */
    mqtts_task_init();

    /*  start motor control task (runs on core 1) */
    motor_control_task_init();

    /*  initialize the Interrupt task */
    interrupt_task_init();
}
