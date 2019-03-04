#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
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

static const char *TAG = "MQTTS_EXAMPLE";

static EventGroupHandle_t wifi_event_group;
const static int CONNECTED_BIT = BIT0;

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
bool json_find_uint8(const cJSON* item, char* str, uint8_t* value)
{
    /*  return with error when there is no JSON content */
    bool return_code = true;
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
                return_code = false;
            }
            break;
        }
    }
    return return_code;
}

/**@brief Callback function when new MQTT data is avaible
 * 
 * @details calls json_find_uint8 to get the new position for the blinds and
 * controls a Relay according to the new position
 */
void received_callback(const esp_mqtt_event_handle_t event)
{
    printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
    printf("DATA=%.*s\r\n", event->data_len, event->data);

    const char *topic = MQTT_BLINDS_TOPIC;
    if (strncmp(event->topic, topic, strlen(topic)) == 0)
    {
        uint8_t value;
        char string[] = "value";
        if (json_find_uint8(cJSON_Parse(event->data),
            string, &value) == 0)
        {
            ESP_LOGI(TAG, "value: %d", (int)value);
            /*  here still misses the code to trigger the relay based on the value */
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
    gpio_pad_select_gpio(5);
    /* Set the GPIO as a push/pull output */
    gpio_set_direction(5, GPIO_MODE_OUTPUT);
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

    /*  nvs flash is used to save variables in the flash
        so they can be restored after reboot */
    nvs_flash_init();

    app_gpio_init();
    app_wifi_init();
    mqtt_app_start();
}
