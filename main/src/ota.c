/**
 * @file ota.c
 * @author Kwon Taeyoung (xlink69@gmail.com)
 * @brief BLE/TCP OTA
 * @version 1.0
 * @date 2024-01-13
 */

#include <string.h>
#include <sys/socket.h>
#include <netdb.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"

#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_ota_ops.h"

#include "nvs.h"
#include "nvs_flash.h"
#include "debug.h"
#include "ota.h"

#define TAG "OTA"

#define BUFFSIZE 1024

static int binary_file_length = 0;
/*an ota data write buffer ready to write to the flash*/
static char ota_write_data[BUFFSIZE + 1] = { 0 };

#if (ENABLE_WIFI_OTA && (WIFI_OTA_TYPE == WIFI_HTTP_OTA || WIFI_OTA_TYPE == WIFI_TCP_OTA))


#define TEXT_BUFFSIZE 1024


/*an packet receive buffer*/
static char text[BUFFSIZE + 1] = { 0 };
/* an image total length*/


#endif	/* #if #if (ENABLE_WIFI_OTA && (WIFI_OTA_TYPE == WIFI_HTTP_OTA || WIFI_OTA_TYPE == WIFI_TCP_OTA)) */

#if (ENABLE_WIFI_OTA && (WIFI_OTA_TYPE == WIFI_HTTP_OTA))
/*socket id*/
static int socket_id = -1;

/*read buffer by byte still delim ,return read bytes counts*/
static int read_until(char *buffer, char delim, int len)
{
//  /*TODO: delim check,buffer check,further: do an buffer length limited*/
    int i = 0;
    while (buffer[i] != delim && i < len) {
        ++i;
    }
    return i + 1;
}

/* resolve a packet from http socket
 * return true if packet including \r\n\r\n that means http packet header finished,start to receive packet body
 * otherwise return false
 * */
static bool read_past_http_header(char text[], int total_len, esp_ota_handle_t update_handle)
{
    /* i means current position */
    int i = 0, i_read_len = 0;
    while (text[i] != 0 && i < total_len) {
        i_read_len = read_until(&text[i], '\n', total_len);
        // if we resolve \r\n line,we think packet header is finished
        if (i_read_len == 2) {
            int i_write_len = total_len - (i + 2);
            memset(ota_write_data, 0, BUFFSIZE);
            /*copy first http packet body to write buffer*/
            memcpy(ota_write_data, &(text[i + 2]), i_write_len);

            esp_err_t err = esp_ota_write( update_handle, (const void *)ota_write_data, i_write_len);
            if (err != ESP_OK) {
                PrintConsole("Error: esp_ota_write failed! err=0x%x\r\n", err);
                return false;
            } else {
                PrintConsole("esp_ota_write header OK\r\n");
                binary_file_length += i_write_len;
            }
            return true;
        }
        i += i_read_len;
    }
    return false;
}

static bool connect_to_http_server()
{
    PrintConsole("Server IP: %s Server Port:%d\r\n", inet_ntoa(st_Config.OtaServerIp), st_Config.OtaServerPort);

    int  http_connect_flag = -1;
    struct sockaddr_in sock_info;

    socket_id = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_id == -1) {
        PrintConsole("Create socket failed!\r\n");
        return false;
    }

    // set connect info
    memset(&sock_info, 0, sizeof(struct sockaddr_in));
    sock_info.sin_family = AF_INET;
    //sock_info.sin_addr.s_addr = st_Config.OtaServerIp;
    sock_info.sin_port = htons((unsigned short)st_Config.OtaServerPort);
	
	struct hostent *hp = gethostbyname(st_Config.OtaServerIp);
	if(hp == NULL) PrintConsole("ERROR, Can't get IP address from DNS : NULL\r\n");
	else
	{
		struct in_addr **addr_list;
		addr_list = (struct in_addr **)hp->h_addr_list;
		if (addr_list[0] == NULL) {
	        PrintConsole("ERROR, IP address list : NULL\r\n");
	    }
		else
		{
			PrintConsole("OTA IP address : %s\r\n", inet_ntoa(addr_list[0]->s_addr));
			sock_info.sin_addr.s_addr = addr_list[0]->s_addr;
		}
	}

    // connect to http server
    http_connect_flag = connect(socket_id, (struct sockaddr *)&sock_info, sizeof(sock_info));
    if (http_connect_flag == -1) {
        PrintConsole("Connect to server failed! errno=%d\r\n", errno);
        close(socket_id);
        return false;
    } else {
        PrintConsole("Connected to server\r\n");
        return true;
    }
    return false;
}

void TaskClientOta(void *pvParameter)
{
    esp_err_t err;
    /* update handle : set by esp_ota_begin(), must be freed via esp_ota_end() */
    esp_ota_handle_t update_handle = 0 ;
    const esp_partition_t *update_partition = NULL;

    PrintConsole("\r\nStarting OTA task...\r\n");

    const esp_partition_t *configured = esp_ota_get_boot_partition();
    const esp_partition_t *running = esp_ota_get_running_partition();

    if (configured != running) {
        PrintConsole("Configured OTA boot partition at offset 0x%08x, but running from offset 0x%08x\r\n",
                 configured->address, running->address);
        PrintConsole("(This can happen if either the OTA boot data or preferred boot image become corrupted somehow.)\r\n");
    }
    PrintConsole("Running partition type %d subtype %d (offset 0x%08x)\r\n",
             running->type, running->subtype, running->address);

	if(pvParameter == NULL)	// http
	{
	    /*connect to http server*/
	    if (connect_to_http_server()) {
	        PrintConsole("Connected to http server\r\n");
	    } else {
	        PrintConsole("Connect to http server failed!\r\n");
			close(socket_id);
			return;
	    }

	    /*send GET request to http server*/
	    const char *GET_FORMAT =
	        "GET %s HTTP/1.0\r\n"
	        "Host: %s:%d\r\n"
	        "User-Agent: esp-idf/1.0 esp32\r\n\r\n";

	    char *http_request = NULL;
	    int get_len = asprintf(&http_request, GET_FORMAT, 
							st_Config.OtaFilename, inet_ntoa(st_Config.OtaServerIp), st_Config.OtaServerPort);

		PrintConsole("Req : %s\r\n", http_request);
	    if (get_len < 0) {
	        PrintConsole("Failed to allocate memory for GET request buffer\r\n");
			close(socket_id);
			return;
	    }
	    int res = send(socket_id, http_request, get_len, 0);
	    free(http_request);

	    if (res < 0) {
	        PrintConsole("Send GET request to server failed\r\n");
			close(socket_id);
			return;
	    } else {
	        PrintConsole("Send GET request to server succeeded\r\n");
	    }
	}

    update_partition = esp_ota_get_next_update_partition(NULL);
    PrintConsole("Writing to partition subtype %d at offset 0x%x\r\n",
             update_partition->subtype, update_partition->address);
    assert(update_partition != NULL);

    err = esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &update_handle);
    if (err != ESP_OK) {
        PrintConsole("esp_ota_begin failed, error=%d\r\n", err);
		if(pvParameter == NULL)	// http
		{
			close(socket_id);
		}
		return;
    }
    PrintConsole("esp_ota_begin succeeded\r\n");

    bool resp_body_start = false, flag = true;
    /*deal with all receive packet*/
    while (flag) {
        memset(text, 0, TEXT_BUFFSIZE);
        memset(ota_write_data, 0, BUFFSIZE);
        int buff_len;

		if(pvParameter == NULL)	// http
		{
			buff_len = recv(socket_id, text, TEXT_BUFFSIZE, 0);
		}
		else	// TCP
		{
			// TCP 용으로 수정할 것.
			buff_len = recv(socket_id, text, TEXT_BUFFSIZE, 0);
		}
		
        if (buff_len < 0) { /*receive error*/
			PrintConsole("Error: receive data error!\r\n");
			return;
				
        } else if (buff_len > 0 && !resp_body_start) { /*deal with response header*/
            memcpy(ota_write_data, text, buff_len);
            resp_body_start = read_past_http_header(text, buff_len, update_handle);
        } else if (buff_len > 0 && resp_body_start) { /*deal with response body*/
            memcpy(ota_write_data, text, buff_len);
            err = esp_ota_write( update_handle, (const void *)ota_write_data, buff_len);
            if (err != ESP_OK) {
                PrintConsole("Error: esp_ota_write failed! err=0x%x\r\n", err);
				return;
	        }
            binary_file_length += buff_len;
            PrintConsole(".");
        } else if (buff_len == 0) {  /*packet over*/
            flag = false;
			if(pvParameter == NULL)	// http
			{
				PrintConsole("\r\nConnection closed, all packets received\r\n");
            	close(socket_id);
			}
			else
			{
				PrintConsole("\r\nall packets received\r\n");
			}
        } else {
            PrintConsole("Unexpected recv result\r\n");
        }
    }

    PrintConsole("Total Write binary data length : %d\r\n", binary_file_length);

    if (esp_ota_end(update_handle) != ESP_OK) {
        PrintConsole("esp_ota_end failed!\r\n");
		return;
    }
    err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK) {
        PrintConsole("esp_ota_set_boot_partition failed! err=0x%x\r\n", err);
		return;
    }
    PrintConsole("Prepare to restart system!\r\n");
	usleep(100000);
    esp_restart();
    return ;
}
#endif /* #if (ENABLE_WIFI_OTA && (WIFI_OTA_TYPE == WIFI_HTTP_OTA)) */


#if (ENABLE_WIFI_OTA && (WIFI_OTA_TYPE == WIFI_TCP_OTA))

#define OTA_SERVER_PORT	12222
#define OTA_BROADCAST_PORT	13333

static int check_ota_command(int ota_socket)
{
	int rcv_len;
	uint8_t buf[4];
	
	rcv_len = recv(ota_socket, buf, 3, 0);
	if(rcv_len <= 0)
	{
		LOGE("OTA command receive ERROR : %d", rcv_len);
		return 0;
	}
	
	if(rcv_len != 3)
	{
		LOGE("OTA command received length ERROR : %d", rcv_len);
		return 0;
	}

	if(memcmp(buf, "ota", 3) != 0)
	{
		LOGE("OTA command ERROR : %02X %02X %02X", buf[0], buf[1], buf[2]);
		return 0;
	}
	
	return 1;
}

static int send_ack_msg(int ota_socket)
{
	uint8_t ack[4] = {'A', 'C', 'K', 0};

	int res = send(ota_socket, ack, 4, 0);

	if(res < 0)
	{
		LOGE("OTA ACK send ERROR : %d", res);
		return 0;
	}

	LOGI("OTA ACK message send OK");

	return 1;
}

extern char *get_my_ip(void);

static void TaskRcvOtaBroadcast(void *arg)
{
    int sockfd;
    struct sockaddr_in servaddr, cliaddr;
    char buffer[128];

    // Creating socket file descriptor
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) 
	{
        LOGE("Broadcast receive socket creation failed");
        return;
    }

    memset(&servaddr, 0, sizeof(servaddr));
    memset(&cliaddr, 0, sizeof(cliaddr));

    // Filling server information
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(OTA_BROADCAST_PORT);
    servaddr.sin_addr.s_addr = INADDR_ANY;

	if(bind(sockfd, (struct sockaddr*)&servaddr, sizeof(servaddr)) < 0)
	{
		LOGE("Broadcast receive socket bind ERROR");
		return;
	}

    int n;
	socklen_t addrlen = sizeof(cliaddr);
    
    // Listening for broadcast
	LOGI("Started broadcast message listening");

    while (1) 
	{
        n = recvfrom(sockfd, buffer, sizeof(buffer), 0, (struct sockaddr *)&cliaddr, &addrlen);
        buffer[n] = 0;
		LOGI("Broadcast message received from %s : %s", inet_ntoa(*(struct in_addr *)&cliaddr.sin_addr.s_addr), buffer);

        if (strcmp(buffer, "REQUEST IP") == 0) 
		{
            char client_response[128];
            snprintf(client_response, sizeof(client_response), "%s", get_my_ip());
            sendto(sockfd, (const char *)client_response, strlen(client_response), MSG_DONTWAIT, (struct sockaddr *)&cliaddr, addrlen);
            LOGI("Sent response: %s", client_response);
        }
		else
		{
			LOGW("Broadcast message ERROR : %d, %s", n, buffer);
		}
    }

    close(sockfd);
}

static void TaskServerOta(void *arg)
{
	esp_err_t err;
	int ServerSocket, OtaClientSocket;
	struct sockaddr_in ServerAddr, ClientAddr;
	int AddrSize;
	const esp_partition_t *update_partition = NULL;
	esp_ota_handle_t update_handle = 0 ;
	
	LOGI("Task OTA server started\r\n");
		
	while(1)
	{
		ServerSocket = socket(PF_INET, SOCK_STREAM, 0);
		if(ServerSocket == -1)
		{
			LOGE("OTA server socket open ERROR!!!");
			usleep(1000000);
		}
		else
		{
			LOGD(TAG, "OTA server socket open : %d",ServerSocket);
			break;
		}
	}

	memset(&ServerAddr, 0, sizeof(struct sockaddr_in));
	ServerAddr.sin_family = AF_INET;
	ServerAddr.sin_addr.s_addr = INADDR_ANY;
	ServerAddr.sin_port = htons(OTA_SERVER_PORT);

	while(1)
	{
		if(bind(ServerSocket, 
		   (struct sockaddr *)&ServerAddr, 
		   sizeof(struct sockaddr_in)) != 0)
		{
			LOGE("Server socket bind() ERROR!!!");
			usleep(1000000);
		}
		else
		{
			ESP_LOGD(TAG, "Server socket bind() OKAY : %d",OTA_SERVER_PORT);
			break;
		}
	}

	while(1)
	{
		if(listen(ServerSocket, 1) == -1)
		{
			LOGE("Server listen ERROR!!!");
			usleep(1000000);
			continue;
		}

		ESP_LOGD(TAG, "Server listen() OKAY");
		break;
	}

	while(1)
	{
		LOGI("Waiting for OTA client...");
		
		AddrSize = sizeof(struct sockaddr_in);

		OtaClientSocket = accept(ServerSocket, (struct sockaddr*)&ClientAddr, (socklen_t *)&AddrSize);
		if(OtaClientSocket == -1)
		{
			LOGE("OTA server accept ERROR!!!");
			close(OtaClientSocket);
			usleep(100000);
			continue;
		}

		LOGI("+++ OTA client connected : %s +++", inet_ntoa(ClientAddr.sin_addr));
		if(!check_ota_command(OtaClientSocket))
		{
			LOGE("OTA command ERROR");
			close(OtaClientSocket);
			usleep(100000);
			continue;
		}

		LOGI("OTA command OK");

		const esp_partition_t *configured = esp_ota_get_boot_partition();
    	const esp_partition_t *running = esp_ota_get_running_partition();

	    if (configured != running) {
	        LOGI("Configured OTA boot partition at offset 0x%x, but running from offset 0x%x\r\n",
	                 configured->address, running->address);
	        LOGI("(This can happen if either the OTA boot data or preferred boot image become corrupted somehow.)\r\n");
	    }
		
	    LOGI("Running partition type %d subtype %d (offset 0x%x)\r\n",
             		running->type, running->subtype, running->address);

		update_partition = esp_ota_get_next_update_partition(NULL);
	    PrintConsole("Writing to partition subtype %d at offset 0x%x\r\n",
	             update_partition->subtype, update_partition->address);
	    if(update_partition == NULL)
	    {
			LOGE("Update partition ERROR");
			close(OtaClientSocket);
			usleep(100000);
			continue;
	    }

	    err = esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &update_handle);
	    if (err != ESP_OK) {
	        LOGE("esp_ota_begin failed, error=%d\r\n", err);
			close(OtaClientSocket);
			usleep(100000);
			continue;
	    }
	    LOGI("esp_ota_begin succeeded");

		if(!send_ack_msg(OtaClientSocket))
		{
			close(OtaClientSocket);
			usleep(100000);
			continue;
		}

		LOGI("Waiting for OTA firmware");
		
	    /*deal with all receive packet*/
	    while (1) {
	        memset(text, 0, TEXT_BUFFSIZE);
	        memset(ota_write_data, 0, BUFFSIZE);
	        int buff_len;

			buff_len = recv(OtaClientSocket, text, TEXT_BUFFSIZE, 0);
			
	        if (buff_len < 0) { /*receive error*/
				LOGE("Error: receive data error!\r\n");
				break;
					
	        } else if (buff_len > 0) {
	            memcpy(ota_write_data, text, buff_len);
	            err = esp_ota_write( update_handle, (const void *)ota_write_data, buff_len);
	            if (err != ESP_OK) {
	                LOGE("Error: esp_ota_write failed! err=0x%x\r\n", err);
					break;
		        }
	            binary_file_length += buff_len;
	            PrintConsole(".");
	        } else {  /*packet over*/
				LOGI("\r\nAll packets received");
				LOGI("Total Write binary data length : %d\r\n", binary_file_length);

				if (esp_ota_end(update_handle) != ESP_OK) {
			        LOGE("esp_ota_end failed!\r\n");
					break;
			    }
			    err = esp_ota_set_boot_partition(update_partition);
			    if (err != ESP_OK) {
			        LOGE("esp_ota_set_boot_partition failed! err=0x%x\r\n", err);
					break;
			    }
			    LOGI("\r\nPrepare to restart system!\r\n\r\n");
				usleep(100000);
			    esp_restart();
				sleep(10);
	        }
	    }

		LOGI("\r\nClose OTA client socket\r\n");
		close(OtaClientSocket);
		usleep(100000);
		continue;
	}
}
#endif /* #if (ENABLE_WIFI_OTA && (WIFI_OTA_TYPE == WIFI_TCP_OTA)) */

#if (ENABLE_BLE_OTA)
static SemaphoreHandle_t semaphore_ota;
static QueueHandle_t msg_queue_ota;

void send_ota_data(uint8_t *data, int len)
{
	BLE_MSG_st msg;

	if(len > 0)
	{
		memcpy(msg.data, data, len);
	}
	
	msg.len = len;
	if(xQueueSend( msg_queue_ota, &msg, 1000 ) == 0)	// 1000 ticks(ms) 동안 전송 시도
	{
		LOGE("OTA message send ERROR");
	}
}

void give_ota_semaphore(void)
{
	xSemaphoreGive(semaphore_ota);
}

static void TaskBleOta(void *arg)
{
	BLE_MSG_st msg;
	esp_err_t err;
	const esp_partition_t *update_partition = NULL;
	esp_ota_handle_t update_handle = 0 ;
	
	while(1)
	{
		xSemaphoreTake(semaphore_ota, portMAX_DELAY);
		
		LOGI("Got semaphore_ota, Begin OTA procedure");
		
		const esp_partition_t *configured = esp_ota_get_boot_partition();
    	const esp_partition_t *running = esp_ota_get_running_partition();

	    if (configured != running) {
	        LOGI("Configured OTA boot partition at offset 0x%08x, but running from offset 0x%08x\r\n",
	                 configured->address, running->address);
	        LOGI("(This can happen if either the OTA boot data or preferred boot image become corrupted somehow.)\r\n");
	    }
		
	    LOGI("Running partition type %d subtype %d (offset 0x%08x)\r\n",
             		running->type, running->subtype, running->address);

		update_partition = esp_ota_get_next_update_partition(NULL);
	    PrintConsole("Writing to partition subtype %d at offset 0x%x\r\n",
	             update_partition->subtype, update_partition->address);
	    if(update_partition == NULL)
	    {
			LOGE("Update partition ERROR");
			usleep(100000);
			continue;
	    }

	    err = esp_ota_begin(update_partition, get_ota_file_size(), &update_handle);
	    if (err != ESP_OK) {
	        LOGE("esp_ota_begin failed, error=%d\r\n", err);
			usleep(100000);
			continue;
	    }

		// set_led_state(LED_STATE_TRANSMIT_BLINK);
	    LOGI("esp_ota_begin succeeded");
		binary_file_length = 0;

		send_json_info();

	    /*deal with all receive packet*/
	    while (1) {
	        int buff_len;

			xQueueReceive(msg_queue_ota, &msg, portMAX_DELAY);
//			LOGI("Ota task received message : %d", msg.len);
			
			buff_len = msg.len;
			
	        if (buff_len < 0) { /*receive error*/
				LOGE("Error: receive data error!, Disconnected?\r\n");
				break;
					
	        } else if (buff_len > 0) {
	            memcpy(ota_write_data, msg.data, buff_len);
	            err = esp_ota_write( update_handle, (const void *)ota_write_data, buff_len);
	            if (err != ESP_OK) {
	                LOGE("Error: esp_ota_write failed! err=0x%x\r\n", err);
					break;
		        }
	            binary_file_length += buff_len;
	            // PrintConsole(".");
				LOGI("Rx Len : %d / %d", binary_file_length, get_ota_file_size());
	        } 
			
			if(binary_file_length >= get_ota_file_size() || buff_len == 0){  /*packet over*/
				LOGI("\r\nAll packets received");
				LOGI("Total Write binary data length : %d\r\n", binary_file_length);

				if (esp_ota_end(update_handle) != ESP_OK) {
			        LOGE("esp_ota_end failed!\r\n");
					break;
			    }
			    err = esp_ota_set_boot_partition(update_partition);
			    if (err != ESP_OK) {
			        LOGE("esp_ota_set_boot_partition failed! err=0x%x\r\n", err);
					break;
			    }

				// set_led_state(LED_STATE_ON);
			    LOGI("\r\nPrepare to restart system!\r\n\r\n");
				usleep(1000000);
			    esp_restart();
				sleep(10);
	        }
		}

		clear_ota_state();
		// set_led_state(LED_STATE_DEFAULT_BLINK);
	}
}

#endif /* #if (ENABLE_BLE_OTA) */

void InitOta(void)
{
	TaskHandle_t handle;
	int ret;

#if (ENABLE_BLE_OTA)
	semaphore_ota = xSemaphoreCreateBinary();
	msg_queue_ota = xQueueCreate(NUMBER_OF_BLE_MSG_QUEUE, sizeof(BLE_MSG_st));
	if(msg_queue_ota == 0)
	{
		LOGE("OTA message queue creation ERROR");
		return;
	}
	ret = xTaskCreatePinnedToCore(&TaskBleOta, "BLEOTA",
            4096, 
            NULL,
            5,
            &handle,
            tskNO_AFFINITY);
	
    if (ret != pdPASS) {
		LOGE("ERROR : CAN'T creat OTA task");
        return;
    }
#endif	// #if (ENABLE_BLE_OTA)

#if (ENABLE_WIFI_OTA)
	ret = xTaskCreatePinnedToCore(&TaskServerOta, "TCPOTA",
			4096, 
			NULL,
			5,
			&handle,
			tskNO_AFFINITY);
	
	if (ret != pdPASS) {
		LOGE("ERROR : CAN'T creat OTA task");
		return;
	}

	ret = xTaskCreatePinnedToCore(&TaskRcvOtaBroadcast, "Broadcast",
			4096, 
			NULL,
			5,
			&handle,
			tskNO_AFFINITY);
	
	if (ret != pdPASS) {
		LOGE("ERROR : CAN'T creat OTA broadcast task");
		return;
	}
#endif	// #if (ENABLE_WIFI_OTA)
}

