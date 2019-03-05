#include "position_queue.h"
#include "esp_log.h"

static const char *TAG = "POSITION_QUEUE";
/* queue that handles new positions */
xQueueHandle position_queue = NULL;

/**@brief Function for initializing the queue thats used to transmit the new
 * position of the blind between the MQTT task and the motor_control task
 */
esp_err_t position_queue_init(void)
{
    position_queue = xQueueCreate(1, sizeof(uint8_t));
    ESP_LOGI(TAG, "Position Queue created");
    return ESP_OK;
}
