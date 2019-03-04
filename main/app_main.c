/*  Example Configuration for the steppermotor driver DRV8825
*
*/
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <math.h>
#include "esp_wifi.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_event_loop.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "driver/gpio.h"

#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"
#include "cJSON.h"

#include "esp_log.h"
#include "mqtt_client.h"

#include "rom/ets_sys.h"

#define MQTT_TOPIC "blindcontrol/#"
#define MQTT_BLINDS_TOPIC "blindcontrol"

/*  END STOP DEFINITIONS */
#define GPIO_HIGH_END_STOP      4   /* End stop for 100% */
#define GPIO_LOW_END_STOP       5   /* End stop for 0% */
#define GPIO_END_STOPS  ((1ULL<<GPIO_HIGH_END_STOP) | (1ULL<<GPIO_LOW_END_STOP))
#define ESP_INTR_FLAG_DEFAULT 0


/* STEPPER DEFINITIONS */
#define STEPPER_COUNT           20000 /* steps needed to open blinds from 0-100% */
#define STEPPER_DELAY           100   /* Minimum delay required between steps */
#define GPIO_STEPPER_ENABLE     21    /* enable stepper driver */
#define GPIO_STEPPER_DIR        22    /* set direction for the stepper */
#define GPIO_STEPPER_STEP       23    /* Pin that triggers steps */
#define GPIO_STEPPER_PINS  ((1ULL<<GPIO_STEPPER_ENABLE) \
    | (1ULL<<GPIO_STEPPER_DIR) | (1ULL<<GPIO_STEPPER_STEP))


static const char *TAG = "MQTTS";
static const char *TAG_MOTOR_CONTROL = "MOTOR_CONTROL";

static EventGroupHandle_t wifi_event_group;
const static int CONNECTED_BIT = BIT0;

static TaskHandle_t motor_control_handle = NULL;

/* queue that handles new positions */
static xQueueHandle position_queue = NULL;
/* queue that handles handles GPIO events from isr */
static xQueueHandle gpio_evt_queue = NULL;

/***************************************************************************************************
 * @section End Stop Interrupts
 **************************************************************************************************/

static esp_err_t set_end_stop_position(uint32_t io_num)
{
    nvs_handle my_nvs_handle;
    esp_err_t error_code;
    uint8_t new_position;

    /*  open NVS flash */
    ESP_LOGI(TAG, "Opening NVS handle");
    error_code = nvs_open("storage", NVS_READWRITE, &app_nvs_handle);
    if (error_code != ESP_OK) return error_code;

    /* Move the blinds to the new position */
    if (io_num == GPIO_HIGH_END_STOP)
    {
        new_position = 100;
    } else if(io_num == GPIO_LOW_END_STOP){
        new_position = 0;
    }

    /*  Write new position to NVS */
    error_code = nvs_set_u8(my_nvs_handle, "new_position", new_position);
    if (error_code != ESP_OK) return error_code;

    /*  Commit written value.
    *   After setting any values, nvs_commit() must be called to ensure changes are written
    *   to flash storage. */
    error_code = nvs_commit(my_nvs_handle);
    if (error_code != ESP_OK) return error_code;

    /*  Close NVS */
    nvs_close(my_nvs_handle);
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
                app_gpio_init();
            }
        }
    }
}


/***************************************************************************************************
 * @section Motor Control
 **************************************************************************************************/

static esp_err_t set_new_position(uint8_t new_position)
{
    nvs_handle my_nvs_handle;
    esp_err_t error_code;

    /*  open NVS flash */
    ESP_LOGI(TAG, "Opening NVS handle");
    error_code = nvs_open("storage", NVS_READWRITE, &app_nvs_handle);
    if (error_code != ESP_OK) return error_code;

    /*  Read old position from NVS
    *   old position defaults to 0 if not set in NVS */
    uint8_t old_position = 0;
    error_code = nvs_get_u8(my_nvs_handle, "old_position", &old_position);
    if (error_code != ESP_OK) return error_code;

    /* Calculate how many Steps are needed */
    int64_t movement = round((new_position - old_position) * (STEPPER_COUNT/100));
    /* set direction for the stepper */
    if (movement > 0)
    {
        gpio_set_level(GPIO_STEPPER_DIR, 1);
    }else if (movement < 0){
        gpio_set_level(GPIO_STEPPER_DIR, 0);
        movement*=-1;
    }

    /*  Move to new position */
    for (int64_t steps; steps < movement; ++steps )
    {
        gpio_set_level(GPIO_STEPPER_STEP, 1);
        vTaskDelay(STEPPER_DELAY / portTICK_PERIOD_MS);
        gpio_set_level(GPIO_STEPPER_STEP, 0);
        TaskDelay(STEPPER_DELAY / portTICK_PERIOD_MS);
    }

    /*  Write new position to NVS */
    error_code = nvs_set_u8(my_nvs_handle, "new_position", new_position);
    if (error_code != ESP_OK) return error_code;

    /*  Commit written value.
    *   After setting any values, nvs_commit() must be called to ensure changes are written
    *   to flash storage. */
    error_code = nvs_commit(my_nvs_handle);
    if (error_code != ESP_OK) return error_code;

    /*  Close NVS */
    nvs_close(my_nvs_handle);
    return ESP_OK;
}


/**@brief Task that reads the new position from the queue an adjusts the blind
 * position
 */
static void motor_control_task(void *arg)
{
    /* activate stepper driver so it does not move */
    gpio_set_level(GPIO_STEPPER_ENABLE, 1);

    uint8_t new_position;

    for(;;)
    {
        /*  if a message could be received from the queue within 60 sek */
        if (xQueueReceive(position_queue, &new_position,
            60000/portTICK_RATE_MS) == pdTRUE)
        {
            ESP_LOGI(TAG, "Received a new value from the queue: %d",
                (int)new_position);
            set_new_position(new_position);

        }else{
            ESP_LOGI(TAG_MOTOR_CONTROL, "Failed to receive a queue value");
        }
        
    }
}

/***************************************************************************************************
 * @section WiFi
 **************************************************************************************************/

/**@brief function handles WiFi events
 */
static esp_err_t wifi_event_handler(void *ctx, system_event_t *event)
{
    switch (event->event_id) {
        case SYSTEM_EVENT_STA_START:
            esp_wifi_connect();
            break;
        case SYSTEM_EVENT_STA_GOT_IP:
            xEventGroupSetBits(wifi_event_group, CONNECTED_BIT);
            break;
        case SYSTEM_EVENT_STA_DISCONNECTED:
            esp_wifi_connect();
            xEventGroupClearBits(wifi_event_group, CONNECTED_BIT);
            break;
        default:
            break;
    }
    return ESP_OK;
}

/***************************************************************************************************
 * @section MQTT
 **************************************************************************************************/

#if CONFIG_BROKER_CERTIFICATE_OVERRIDDEN == 1
static const uint8_t iot_eclipse_org_pem_start[]  = "-----BEGIN CERTIFICATE-----\n" CONFIG_BROKER_CERTIFICATE_OVERRIDE "\n-----END CERTIFICATE-----";
#else
extern const uint8_t iot_eclipse_org_pem_start[]   asm("_binary_iot_eclipse_org_pem_start");
#endif
extern const uint8_t iot_eclipse_org_pem_end[]   asm("_binary_iot_eclipse_org_pem_end");



/**@brief get a value from JSON strings
 */
static esp_err_t json_find_uint8(const cJSON* item, char* str, uint8_t* value)
{
    /*  return with error when there is no JSON content */
    esp_err_t error_code = ESP_FAIL;
    if (!item) return return_code;

    cJSON *subitem = item->child;

    /*  split objectpath so the objectnames can be compared */
    char* token = strtok_r(str, "/", &str);

    /*  while there are objects to compare */
    while (subitem)
    {
        /*  when the object is wrong the next object gets compared */
        if(strncmp(subitem->string, token, strlen(token)))
        {
            subitem = subitem->next;
        /*  if the object is right the childobjects get compared */
        }else if (subitem->child && str){
            subitem = subitem->child;
            token = strtok_r(str, "/", &str);
        /*  when the whole objectpath is found and the objects value is a integer */
        }else if (!str && cJSON_IsNumber(subitem)){
            const int new_value = subitem->valueint;
            /*  value for Blinds can only be 0-100% */
            if (new_value <= 100 && new_value >= 0)
            {
                *value = (uint8_t)new_value;
                error_code = ESP_OK;
            }
            break;
        }
    }
    return error_code;
}

/**@brief Callback function when new MQTT data is avaible
 * 
 * @details calls json_find_uint8 to get the new position for the blinds and
 * controls a Relay according to the new position
 */
static void received_callback(const esp_mqtt_event_handle_t event)
{
    printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
    printf("DATA=%.*s\r\n", event->data_len, event->data);

    const char *topic = MQTT_BLINDS_TOPIC;
    if (strncmp(event->topic, topic, strlen(topic)) == 0)
    {
        uint8_t value;
        char string[] = "value";

        esp_err_t error = json_find_uint8(
            cJSON_Parse(event->data), string, &value);
        if (error_code == ESP_OK)
        {
            ESP_LOGI(TAG, "writing value: %d to the queue", (int)value);

            /* overwrite the item in the position queue when it´s still
            in there (motor control was to slow) or add it when the queue
            is already empty */
            xQueueOverwrite(position_queue, &value);
        }else{
            ESP_LOGI(TAG, "JSON ERROR: %d", error);
        }
    }
}

/**@brief Function handles all MQTT events
 * 
 * @details handles events like receiving data
 */
static esp_err_t mqtt_event_handler(esp_mqtt_event_handle_t event)
{
    esp_mqtt_client_handle_t client = event->client;
    int msg_id;
    switch (event->event_id) {
        /*  when connected subscribe to a topic */
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
            msg_id = esp_mqtt_client_subscribe(client, MQTT_TOPIC, 0);
            ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);
            break;
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
            break;
        case MQTT_EVENT_SUBSCRIBED:
            ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_UNSUBSCRIBED:
            ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_PUBLISHED:
            ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
            break;
        /*  when data is avaible call the corresponding callback functions */
        case MQTT_EVENT_DATA:
            ESP_LOGI(TAG, "MQTT_EVENT_DATA");
            received_callback(event);
            break;
        case MQTT_EVENT_ERROR:
            ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
            break;
        default:
            ESP_LOGI(TAG, "Other event id:%d", event->event_id);
            break;
    }
    return ESP_OK;
}


/***************************************************************************************************
 * @section Initialization
 **************************************************************************************************/

/**@brief Function for initializing the nvs flash
 * 
 * @details nvs flash is used to save variables in the flash
 * so they can be restored after reboot
 */
static void app_nvs_flash_init(void)
{
    /*  trying to initialize nvs flash */
    esp_err_t error_code = nvs_flash_init();

    /*  When its invalid try to erase it */
    if (error_code == ESP_ERR_NVS_NO_FREE_PAGES
        || error_code == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        /*  Try to erade nvs partition */
        ESP_ERROR_CHECK(nvs_flash_erase());
        error_code = nvs_flash_init();
    }
    ESP_ERROR_CHECK( error_code );
}

/**@brief Function for initializing the MQTTS Connection
 * 
 * @details starts a MQTTS Connection using Username + Password and TLS.
 */
static void mqtt_app_start(void)
{
    /*  set all config parameters */
    const esp_mqtt_client_config_t mqtt_cfg = {
        .host = CONFIG_BROKER_HOST,
        .port = CONFIG_BROKER_PORT,
        .username = CONFIG_BROKER_USERNAME,
        .password = CONFIG_BROKER_PASSWORD,
        .event_handle = mqtt_event_handler,
        //.cert_pem = (const char *)iot_eclipse_org_pem_start,
    };

    ESP_LOGI(TAG, "[APP] Free memory: %d bytes", esp_get_free_heap_size());
    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
    esp_err_t error = esp_mqtt_client_start(client);
    ESP_LOGI(TAG, "[APP] Error %d", error);
}

/**@brief Function for initializing the Wifi connection using WPA2
 */
static void app_wifi_init(void)
{
    tcpip_adapter_init();
    wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_event_loop_init(wifi_event_handler, NULL));
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    /*  only store config in RAM */
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = CONFIG_WIFI_SSID,
            .password = CONFIG_WIFI_PASSWORD,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
    ESP_LOGI(TAG, "start the WIFI SSID:[%s]", CONFIG_WIFI_SSID);
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "Waiting for wifi");
    xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT, false, true, portMAX_DELAY);
}

/**@brief Function for initializing the used GPIO Pins
 */
static void app_gpio_init(void)
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

}

/**@brief Function for initializing the Task of the motor control
 */
static void motor_control_start(void)
{
    xTaskCreate(
        motor_control_task,     /* Task function */
        "MOTOR_CONTROL",        /* Name of task */
        2048,                   /* Stack size of task */
        NULL,                   /* parameter of the task */
        1,                      /* priority of the task (high is important) */
        &motor_control_handle   /* Task handle to keep track of created Task */
    )
}

/**@brief Function for initializing the queue thats used to transmit the new
 * position of the blind between the MQTT task and the motor_control task
 */
static void position_queue_init(void)
{
    position_queue = xQueueCreate(1, sizeof(uint8_t));
}

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
    app_nvs_flash_init();

    /*  initialize the used gpio pins */
    app_gpio_init();

    /*  start Wifi task (runs on core 0) */
    app_wifi_init();

    /*  create Queue for communication between the mqtt and motor control tasks */
    position_queue_init();

    /*  start MQTT task (runs on core 0) */
    mqtt_app_start();

    /*  start motor control task (runs on core 1) */
    motor_control_start();
}
