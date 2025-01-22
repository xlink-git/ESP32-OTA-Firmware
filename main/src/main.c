/* Scan Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

/**
 * @file main.c
 * @author Kwon Taeyoung (xlink69@gmail.com)
 * @brief BLE/TCP OTA
 * @version 1.0
 * @date 2024-01-13
 */

#include <string.h>
#include <sys/time.h>
#include <sys/unistd.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_app_desc.h"
#include "nvs_flash.h"
#include "driver/uart.h"

#include "debug.h"
#include "ota.h"
#include "wifi.h"

static char strVersion[64];


char *get_version_string(void)
{
	return strVersion;
}

void app_main(void)
{
	esp_err_t ret;

    const esp_app_desc_t *app_desc = esp_app_get_description();

	// version(PROJECT_VER) is defined in CMakeLists.txt
	snprintf(strVersion, 64, "%s %s %s", app_desc->version, app_desc->date, app_desc->time);

    uart_driver_install(UART_NUM_0, 1024, 0, 0, NULL, 0);

	ret = nvs_flash_init();
	LOGI("NVS default partition init : %d, %s", ret, esp_err_to_name(ret));
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
		LOGI("NVS default partition erase");
		nvs_flash_erase();
		ret = nvs_flash_init();
		LOGI("nvs default partition reinit : %d, %s", ret, esp_err_to_name(ret));
    }
    ESP_ERROR_CHECK(ret);
    
#if defined(ENABLE_WIFI)
	wifi_init_softap();
	// wifi_init_sta();
#endif

    bt_ble_init();
    usleep(10000);
    InitOta();
    InitDebug();

	while(1)
	{
		vTaskDelay(1000);
	}
}
