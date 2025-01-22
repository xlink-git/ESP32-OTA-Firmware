#if !defined (__OTA_H__)

#define __OTA_H__

/*---------------------------- User define -------------------------------*/
#define ENABLE_BLE_OTA	1
#define ENABLE_WIFI_OTA	1

#define WIFI_HTTP_OTA	1
#define WIFI_TCP_OTA	2

#define WIFI_OTA_TYPE	WIFI_TCP_OTA

#define MAX_FIRMWARE_SIZE	0x130000

#define NUMBER_OF_BLE_MSG_QUEUE	5
#define QUEUE_DATA_SIZE	512

typedef struct {
	int len;
	uint8_t data[QUEUE_DATA_SIZE];
} BLE_MSG_st;

/*---------------------------- Variables ---------------------------------*/

/*-------------------------- Function declares ---------------------------*/
void InitOta(void);
void bt_ble_init(void);
void print_le_status(void);
int is_ble_connected(void);
uint8_t *ble_get_mac_address(void);

esp_err_t _nordic_uart_send( uint8_t *message, int len);
int get_ota_file_size(void);
int is_ota_ready(void);
void clear_ota_state(void);

void give_ota_semaphore(void);
void send_ota_data(uint8_t *data, int len);
int json_parsing(char *json_string);
void test_mode_off(void);
void send_json_info(void);
void send_json_working_state(void);
void send_json_light_onoff(void);
void send_json_test_mode_off(void);

#endif  /* End_of __OTA_H__ */

