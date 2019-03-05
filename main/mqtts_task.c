#include "mqtts_task.h"
#include "position_queue.h"
#include "mqtt_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "cJSON.h"

#define MQTT_TOPIC "blindcontrol/#"
#define MQTT_BLINDS_TOPIC "blindcontrol"

static const char *TAG = "MQTTS_TASK";

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
    if (!item) return error_code;

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

        esp_err_t error_code = json_find_uint8(
            cJSON_Parse(event->data), string, &value);
        if (error_code == ESP_OK)
        {
            ESP_LOGI(TAG, "writing value: %d to the queue", (int)value);

            /* overwrite the item in the position queue when it´s still
            in there (motor control was to slow) or add it when the queue
            is already empty */
            xQueueOverwrite(position_queue, &value);
        }else{
            ESP_LOGI(TAG, "JSON ERROR: %d", error_code);
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

/**@brief Function for initializing the MQTTS Connection
 * 
 * @details starts a MQTTS Connection using Username + Password and TLS.
 */
esp_err_t mqtts_task_init(void)
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
    return ESP_OK;
}
