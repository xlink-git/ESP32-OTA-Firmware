/**
 * @file bt_ble.c
 * @author Kwon Taeyoung (xlink69@gmail.com)
 * @brief BLE communication
 * @version 1.0
 * @date 2024-01-13
 */
#include <sys/unistd.h>
#include "esp_system.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "driver/uart.h"
#include "string.h"

#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "console/console.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "esp_mac.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "debug.h"
#include "ota.h"

/*---------------------------- User define -------------------------------*/
#define BLE_ADV_NAME "ESP32_BLE"

static int BLE_SEND_MTU = 18;

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define B0(x) ((x)&0xFF)
#define B1(x) (((x) >> 8) & 0xFF)
#define B2(x) (((x) >> 16) & 0xFF)
#define B3(x) (((x) >> 24) & 0xFF)
#define B4(x) (((x) >> 32) & 0xFF)
#define B5(x) (((x) >> 40) & 0xFF)

// clang-format off
#define UUID128_CONST(a32, b16, c16, d16, e48) \
  BLE_UUID128_INIT( \
    B0(e48), B1(e48), B2(e48), B3(e48), B4(e48), B5(e48), \
    B0(d16), B1(d16), B0(c16), B1(c16), B0(b16), \
    B1(b16), B0(a32), B1(a32), B2(a32), B3(a32), \
  )
// clang-format off
enum nordic_uart_callback_type {
  NORDIC_UART_DISCONNECTED,
  NORDIC_UART_CONNECTED,
};

typedef void (*uart_receive_callback_t)(struct ble_gatt_access_ctxt *ctxt);

/*---------------------------- Variables ---------------------------------*/
static const char *TAG = "BLE";

static QueueHandle_t msg_queue_ble;

// [xlink] 240619 : Nordic UART Service UUID
static const ble_uuid128_t SERVICE_UUID = UUID128_CONST(0x6E400001, 0xB5A3, 0xF393, 0xE0A9, 0xE50E24DCCA9E);
static const ble_uuid128_t CHAR_UUID_RX = UUID128_CONST(0x6E400002, 0xB5A3, 0xF393, 0xE0A9, 0xE50E24DCCA9E);
static const ble_uuid128_t CHAR_UUID_TX = UUID128_CONST(0x6E400003, 0xB5A3, 0xF393, 0xE0A9, 0xE50E24DCCA9E);

static uint8_t ble_addr_type;

static uint16_t ble_conn_hdl = BLE_HS_CONN_HANDLE_NONE;
static uint16_t notify_char_attr_hdl;

static void (*_nordic_uart_callback)(enum nordic_uart_callback_type callback_type) = NULL;
//static uart_receive_callback_t _uart_receive_callback = NULL;

static int _uart_receive(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg);
static int _uart_noop(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg);

static const struct ble_gatt_svc_def gat_svcs[] = {
    {.type = BLE_GATT_SVC_TYPE_PRIMARY,
     .uuid = &SERVICE_UUID.u,
     .characteristics =
         (struct ble_gatt_chr_def[]){
             {.uuid = (ble_uuid_t *)&CHAR_UUID_RX,
              .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
              .access_cb = _uart_receive},
             {.uuid = (ble_uuid_t *)&CHAR_UUID_TX,
              .flags = BLE_GATT_CHR_F_NOTIFY,
              .val_handle = &notify_char_attr_hdl,
              .access_cb = _uart_noop},
             {0},
         }},
    {0}};

/*-------------------------- Function declares ---------------------------*/
static int ble_gap_event_cb(struct ble_gap_event *event, void *arg);

static int _uart_receive(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg) {
	static BLE_MSG_st msg;
	uint8_t *ptr;
	uint16_t total_len;

	ptr = ctxt->om->om_data;
	total_len = ctxt->om->om_len;
	
	if(ctxt->om->om_len > QUEUE_DATA_SIZE)
	{
		if(is_ota_ready())
		{
			send_ota_data(ptr, total_len);
			ptr += QUEUE_DATA_SIZE;
			total_len -= QUEUE_DATA_SIZE;
		}
		else
		{
			memcpy(msg.data, ptr, QUEUE_DATA_SIZE);
			msg.len = QUEUE_DATA_SIZE;
			ptr += QUEUE_DATA_SIZE;
			total_len -= QUEUE_DATA_SIZE;
			if(xQueueSend( msg_queue_ble, &msg, 1000 ) == 0)	// 1000 ticks(ms) 동안 전송 시도
			{
				LOGE("BLE Rx message send ERROR");
			}
		}
	}

	if(is_ota_ready())
	{
		send_ota_data(ptr, total_len);
	}
	else
	{
		memcpy(msg.data, ptr, total_len);
		msg.len = total_len;

		if(xQueueSend( msg_queue_ble, &msg, 1000 ) == 0)	// 1000 ticks(ms) 동안 전송 시도
		{
			LOGE("BLE Rx message send ERROR");
		}
	}
	
	return 0;
}

// notify GATT callback is no operation.
static int _uart_noop(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg) {
  return 0;
}

// Smart patch 용 UUID
// static ble_uuid16_t ADD_SERVICE_UUID16 = BLE_UUID16_INIT(0x4444);

static void ble_app_advertise(void) {
  
  const char *name = ble_svc_gap_device_name();
  int err;

#if 1	//[[ Kwon TaeYoung 2023/03/29_BEGIN -- Short advertising name 사용하지 않음
  struct ble_hs_adv_fields  fields;
  
  memset(&fields, 0, sizeof(fields));

  // fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_DISC_LTD;
  fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;

  fields.tx_pwr_lvl_is_present = 1;
  fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;

//   char short_name[6]; // 5 plus zero byte
  
//   strncpy(short_name, name, sizeof(short_name));
//   short_name[sizeof(short_name) - 1] = '\0';
//   fields.name = (uint8_t *)short_name;
//   fields.name_len = strlen(short_name);
//   if (strlen(name) <= sizeof(short_name) - 1) {
//     fields.name_is_complete = 1;
//   } else {
//     fields.name_is_complete = 0;
//   }

  fields.uuids128_is_complete = 1;
  fields.uuids128 = &SERVICE_UUID;
  fields.num_uuids128 = 1;

  err = ble_gap_adv_set_fields(&fields);
  if (err) {
    LOGE("ble_gap_adv_set_fields, err %d", err);
  }
#endif	//]] Kwon TaeYoung 2023/03/29_END -- 0

	struct ble_hs_adv_fields fields_ext;
	memset(&fields_ext, 0, sizeof(fields_ext));

	//Kwon TaeYoung 2023/03/29 : Smart patch 용 특별 UUID 설정, 실제 서비스는 없음
	// fields_ext.num_uuids16 = 1;
	// fields_ext.uuids16 = &ADD_SERVICE_UUID16;
	
	// fields_ext.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
	fields_ext.name = (uint8_t *)name;
	fields_ext.name_len = strlen(name);
	fields_ext.name_is_complete = 1;

	// fields_ext.tx_pwr_lvl = 0;
	// fields_ext.tx_pwr_lvl_is_present = 1;

	// [xlink] 240619 : 아래 추가시 adv 패킷에 03 1A 9001 추가됨, Appreance type 0x0190 : Heart rate sensor(Belt)
	// fields_ext.adv_itvl = 400;
	// fields_ext.adv_itvl_is_present = 1;
	
	err = ble_gap_adv_rsp_set_fields(&fields_ext);
	if (err) {
		LOGE("ble_gap_adv_rsp_set_fields fields_ext, name might be too long, err %d", err);
	}

  struct ble_gap_adv_params adv_params;
  memset(&adv_params, 0, sizeof(adv_params));

  adv_params.itvl_min = 300;
  adv_params.itvl_max = 400;
  
  adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
  adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

  err = ble_gap_adv_start(ble_addr_type, NULL, BLE_HS_FOREVER, &adv_params, ble_gap_event_cb, NULL);
  if (err == BLE_HS_EALREADY) 
  {
	LOGI("ADV already started");
  }
  else if(err != 0)
  {
    LOGE("Advertising start failed: err %d", err);
  }
}

static void bleprph_print_conn_desc(struct ble_gap_conn_desc *desc)
{
    LOGI(" conn_itvl=%d conn_latency=%d supervision_timeout=%d "
                "encrypted=%d authenticated=%d bonded=%d\n",
                desc->conn_itvl, desc->conn_latency,
                desc->supervision_timeout,
                desc->sec_state.encrypted,
                desc->sec_state.authenticated,
                desc->sec_state.bonded);
}

static int ble_connected;
int is_ble_connected(void)
{
	return ble_connected;
}

static uint8_t bt_mac_addr[6];

uint8_t *ble_get_mac_address(void)
{
	return bt_mac_addr;
}

static int conn_count;
static int ble_gap_event_cb(struct ble_gap_event *event, void *arg) 
{
	struct ble_gap_conn_desc desc;
	
  	switch (event->type) 
	{
  		case BLE_GAP_EVENT_CONNECT:
	    	LOGI("BLE_GAP_EVENT_CONNECT %s", event->connect.status == 0 ? "OK" : "Failed");
	    	if (event->connect.status == 0) 
			{
				conn_count++;

				uint16_t pre_handle = ble_conn_hdl;
	      		ble_conn_hdl = event->connect.conn_handle;
	      		if(_nordic_uart_callback)
	      		{
	        		_nordic_uart_callback(NORDIC_UART_CONNECTED);
	      		}

				struct ble_gap_upd_params param;
				param.itvl_max = 12;
				param.itvl_min = 12;
				param.latency = 0;
				param.min_ce_len = 24;
				param.max_ce_len = 48;
				param.supervision_timeout = 100;
				ble_gap_update_params(ble_conn_hdl, &param);
				ble_connected = 1;

				// [xlink] 241016 : 연결된 후에도 Adv 시작하여 새로운 접속 처리하기 위해
				ble_app_advertise();

				ble_gap_terminate(pre_handle, BLE_ERR_REM_USER_CONN_TERM);  // 기존 연결 해제
	    	} 
			else 
			{
	      		ble_app_advertise();
	    	}
    	break;

		case BLE_GAP_EVENT_DISCONNECT:
//    		_nordic_uart_linebuf_append('\003'); // send Ctrl-C
			conn_count--;
    		LOGI("BLE_GAP_EVENT_DISCONNECT : %d", conn_count);
			if(conn_count > 0)
			{
				conn_count = 1;
				return 0;
			}

			ble_connected = 0;

    		if (_nordic_uart_callback)
    		  	_nordic_uart_callback(NORDIC_UART_DISCONNECTED);
    		ble_app_advertise();
			if(is_ota_ready()) send_ota_data(NULL, -1);

			ble_gap_set_prefered_default_le_phy(BLE_HCI_LE_PHY_2M_PREF_MASK, BLE_HCI_LE_PHY_2M_PREF_MASK);
    	break;

		case BLE_GAP_EVENT_ADV_COMPLETE:
    		LOGI("BLE_GAP_EVENT_ADV_COMPLETE");
    		ble_app_advertise();
    	break;

		case BLE_GAP_EVENT_SUBSCRIBE:
    		LOGI("BLE_GAP_EVENT_SUBSCRIBE");
    	break;

		case BLE_GAP_EVENT_CONN_UPDATE:
			LOGI("BLE_GAP_EVT_CONN_PARAM_UPDATE received");
			ble_gap_conn_find(event->conn_update.conn_handle, &desc);
			bleprph_print_conn_desc(&desc);
		break;

		case BLE_GAP_EVENT_MTU:
	        LOGI("mtu update event; conn_handle=%d cid=%d mtu=%d\n",
	                    event->mtu.conn_handle,
	                    event->mtu.channel_id,
	                    event->mtu.value);
			if(event->mtu.value <= 20)
				BLE_SEND_MTU = event->mtu.value - 2;
			else
				BLE_SEND_MTU = event->mtu.value - 4;
			LOGI("Max Tx size : %d", BLE_SEND_MTU);
		break;
		
		case BLE_GAP_EVENT_PHY_UPDATE_COMPLETE:
        	LOGI("LE PHY Update completed; status=%d conn_handle=%d tx_phy=%d "
                    "rx_phy = %d\n", event->phy_updated.status,
                    event->phy_updated.conn_handle, event->phy_updated.tx_phy,
                    event->phy_updated.rx_phy);
  		default:
    	break;
  	}
  
  	return 0;
}

static void ble_app_on_sync_cb(void) {
  int ret = ble_hs_id_infer_auto(0, &ble_addr_type);
  if (ret != 0) {
    LOGE("Error ble_hs_id_infer_auto: %d", ret);
  }
  ble_app_advertise();
}

// https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/bluetooth/nimble/index.html#_CPPv434esp_nimble_hci_and_controller_initv
static void ble_host_task(void *param) {
  nimble_port_run(); // This function will return only when nimble_port_stop() is executed.
  nimble_port_freertos_deinit();
//  _nordic_uart_buf_deinit();
}

// Split the message in BLE_SEND_MTU and send it.
esp_err_t _nordic_uart_send( uint8_t *message, int len) {
	if(!ble_connected) return ESP_FAIL;
	
  if (len == 0)
    return ESP_OK;
  // Split the message in BLE_SEND_MTU and send it.
  for (int i = 0; i < len; i += BLE_SEND_MTU) {
    int err;
    struct os_mbuf *om;
    int err_count = 0;
  do_notify:
    om = ble_hs_mbuf_from_flat(&message[i], MIN(BLE_SEND_MTU, len - i));
    err = ble_gattc_notify_custom(ble_conn_hdl, notify_char_attr_hdl, om);
    if (err == BLE_HS_ENOMEM && err_count++ < 10) {
      vTaskDelay(100 / portTICK_PERIOD_MS);
      goto do_notify;
    }
    if (err)
    {
		LOGE("BLE send ERROR : %d", err);
      return ESP_FAIL;
    }
  }
  return ESP_OK;
}

esp_err_t _nordic_uart_start(const char *device_name, void (*callback)(enum nordic_uart_callback_type callback_type)) {
  // already initialized will return ESP_FAIL
//  if (_nordic_uart_linebuf_initialized()) {
//    LOGE("Already initialized");
//    return ESP_FAIL;
//  }
//
//  _nordic_uart_callback = callback;
//  _nordic_uart_buf_init();

  // Initialize NimBLE, idf V4.x
  // esp_err_t ret = esp_nimble_hci_and_controller_init();
  // if (ret != ESP_OK) {
  //   LOGE("esp_nimble_hci_and_controller_init() failed with error: %d", ret);
  //   return ESP_FAIL;
  // }
  nimble_port_init();

  // Initialize the NimBLE Host configuration
  // Bluetooth device name for advertisement
  ble_svc_gap_device_name_set(device_name);
  ble_svc_gap_init();
  ble_svc_gatt_init();

  ble_gatts_count_cfg(gat_svcs);
  ble_gatts_add_svcs(gat_svcs);

  ble_hs_cfg.sync_cb = ble_app_on_sync_cb;

  // Create NimBLE thread
  nimble_port_freertos_init(ble_host_task);

  return ESP_OK;
}

void _nordic_uart_stop(void) {
  esp_err_t rc = ble_gap_adv_stop();
  if (rc) {
    // if already stoped BLE, some error is raised. but no problem.
    ESP_LOGD(TAG, "Error in stopping advertisement with err code = %d", rc);
  }

  int ret = nimble_port_stop();
  if (ret == 0) {
    nimble_port_deinit();
    // idf V4.x
    // ret = esp_nimble_hci_and_controller_deinit();
    // if (ret != ESP_OK) {
    //   LOGE("esp_nimble_hci_and_controller_deinit() failed with error: %d", ret);
    // }
  }

  _nordic_uart_callback = NULL;
}

void print_le_status(void)
{
	int ret;
	uint8_t tx_phy, rx_phy;
	
	ret = ble_gap_read_le_phy(ble_conn_hdl, &tx_phy, &rx_phy);
	LOGI("Connection PHY : %d/%d, ret : %d", tx_phy, rx_phy, ret);
	// ble_printf("Connection PHY : %d/%d, ret : %d\n", tx_phy, rx_phy, ret);
}

static void TaskBle(void *arg)
{
	BLE_MSG_st msg;
	
	while(1)
	{
		if( xQueueReceive(msg_queue_ble, &msg, portMAX_DELAY))
		{
			LOGI("BLE task received message : %d", msg.len);
			msg.data[msg.len] = 0;
			CommandProcess((char *)msg.data);
		}
	}
}

void bt_ble_init(void)
{
	char adv_name[32] = {0};

	strcpy(adv_name, BLE_ADV_NAME);
	
	LOGI("BLE Adv name : %s", adv_name);

	_nordic_uart_start(adv_name, NULL);

	TaskHandle_t handle;
	int ret;

	msg_queue_ble = xQueueCreate(NUMBER_OF_BLE_MSG_QUEUE, sizeof(BLE_MSG_st));
	if(msg_queue_ble == 0)
	{
		LOGE("BLE Rx message queue creation ERROR");
		return;
	}
	
	ret = xTaskCreatePinnedToCore(&TaskBle, "BLE-Rx",
            8192, 
            NULL,
            5,
            &handle,
            tskNO_AFFINITY);
	
    if (ret != pdPASS) {
		LOGE("ERROR : CAN'T creat task");
        return;
    }
}


