#include "nvs_flash_initialize.h"
#include "nvs_flash.h"
#include "esp_log.h"

static const char *TAG = "NVS_FLASH_INITIALIZE";

/**@brief Function for initializing the nvs flash
 * 
 * @details nvs flash is used to save variables in the flash
 * so they can be restored after reboot
 */
esp_err_t nvs_flash_initialize(void)
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
    return ESP_OK;
}
