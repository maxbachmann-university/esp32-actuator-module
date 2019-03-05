#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "cJSON.h"
#include "driver/gpio.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_log.h"

#define FIRMWARE_VERSION 0.1

static const char *TAG = "OTA_TASK";

/*  server tls certificate */
extern const char server_cert_pem_start[] asm("_binary_ota_tls_cert_pem_start");
extern const char server_cert_pem_end[] asm("_binary_ota_tls_cert_pem_end");

/*  receive buffer */
char rcv_buffer[200];

/*  esp_http_client event handler */
static esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{
	switch(evt->event_id)
    {
        case HTTP_EVENT_ERROR:
            break;
        case HTTP_EVENT_ON_CONNECTED:
            break;
        case HTTP_EVENT_HEADER_SENT:
            break;
        case HTTP_EVENT_ON_HEADER:
            break;
        case HTTP_EVENT_ON_DATA:
            if (!esp_http_client_is_chunked_response(evt->client))
            {
				strncpy(rcv_buffer, (char*)evt->data, evt->data_len);
            }
            break;
        case HTTP_EVENT_ON_FINISH:
            break;
        case HTTP_EVENT_DISCONNECTED:
            break;
    }
    return ESP_OK;
}

static esp_err_t parse_update_info(esp_http_client_config_t* ota_client_config)
{
    /*  parse the json file */
	cJSON *json = cJSON_Parse(rcv_buffer);

    /*  return with error when there is no JSON content */
    esp_err_t error_code = ESP_FAIL;
	if(!json) return error_code;

	cJSON *version = cJSON_GetObjectItemCaseSensitive(json, "version");
	cJSON *file = cJSON_GetObjectItemCaseSensitive(json, "file");
				
	if(cJSON_IsNumber(version))
	{	
    	double new_version = version->valuedouble;
        /*  when Version is newer */
		if(new_version > FIRMWARE_VERSION)
        {
            ESP_LOGI(TAG,
                "current firmware version (%.1f) is lower than the available one (%.1f)",
                FIRMWARE_VERSION, new_version);

            /*  when there is a url for the download */
			if(cJSON_IsString(file) && (file->valuestring != NULL))
            {
				ESP_LOGI(TAG, "Downloading and installing new firmware");

				ota_client_config->url = file->valuestring;
				ota_client_config->cert_pem = server_cert_pem_start;

                error_code = ESP_OK;
			}
		} else {
            ESP_LOGI(TAG, "Firmware is on the latest version");
        }
	}
	return error_code;
}

/**@brief Task that checks for updates and installs them automatically
 */
static void ota_update_task(void *args)
{
	
	for(;;)
    {
		ESP_LOGI(TAG, "Getting latest firmware information");
	
		/*  configure the esp_http_client */
		esp_http_client_config_t config = {
            .url = CONFIG_UPDATE_JSON_URL,
            .event_handler = _http_event_handler,
            .cert_pem = server_cert_pem_start,
		};
		esp_http_client_handle_t client = esp_http_client_init(&config);
	
		/*  downloading the firmware information */
		esp_err_t error_code = esp_http_client_perform(client);
		if(error_code == ESP_OK)
        {
            esp_http_client_config_t ota_client_config;
			error_code = parse_update_info(&ota_client_config);
            if (error_code == ESP_OK)
            {
                error_code = esp_https_ota(&ota_client_config);
				if (error_code == ESP_OK)
                {
					ESP_LOGI(TAG, "OTA OK, restarting...");
                    // might cause problems to just restart when the position is getting changed at the same time
                    // should allow motor_control_task to finish first using a event group
					esp_restart();
				} else {
					ESP_LOGI(TAG, "OTA Failed");
				}
            }
		} else {
            ESP_LOGI(TAG, "Failed to get the latest firmware information");
        }

		/*  cleanup */
		esp_http_client_cleanup(client);
        /*  pause task for 30 sec */
        vTaskDelay(30000 / portTICK_PERIOD_MS);
    }
}

/**@brief Function for initializing the Task of the over the air updates (ota)
 */
esp_err_t ota_update_task_init()
{
    ESP_LOGI(TAG, "[APP] firmware version: %f", FIRMWARE_VERSION);

	xTaskCreate(
        &ota_update_task,     /* Task function */
        "ota_update_task",    /* Name of task */
        8192,                   /* Stack size of task */
        NULL,                   /* parameter of the task */
        3,                      /* priority of the task (high is important) */
        NULL);                  /* Task handle to keep track of created Task */
    
    return ESP_OK;
}
