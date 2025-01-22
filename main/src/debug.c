/****************************************************************************/
//  File    : debug.c
//---------------------------------------------------------------------------
//  Scope   :  
// 
//  Description: 
//             UART & TCP Debug functions
//  
//  
//  History : 
//---------------------------------------------------------------------------
//   Date      | Author | Version |  Modification 
//-------------+--------+---------+------------------------------------------
// 4 Oct 2021 |  Kwon Taeyoung   |   1.0   |  Creation
/****************************************************************************/
#include <sys/time.h>
#include <sys/unistd.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"
#include "arpa/inet.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_err.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "driver/rtc_io.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_continuous.h"
#include "driver/gpio.h"
#include "spi_flash_mmap.h"
#include <sys/socket.h>
#include "debug.h"
#include "ota.h"

#define TAG	"debug"

#define CMD_REBOOT		"reboot"
#define CMD_TIME		"time"

#if defined(ENABLE_WIFI)
static int TcpConnected;
static int TelnetClientSocket;
#endif	// #if defined(ENABLE_WIFI)

void PrintConsole(const char *format, ...)
{
	char outBuff[512];
	va_list args;
	int len = 0;

	if(strcmp(format, ".") != 0)
	{
		int tick = xTaskGetTickCount();
		len = sprintf(outBuff, "[%5d.%02d] ", tick / configTICK_RATE_HZ, tick % configTICK_RATE_HZ);
	}
	
	va_start( args, format );

	vsprintf( &outBuff[len], format, args );

#if defined(ENABLE_WIFI)
	if(TcpConnected)
	{
		PrintTcp("%s", outBuff);
	}
	else
#endif	// #if defined(ENABLE_WIFI)
	{
		uart_write_bytes(UART_NUM_0, (const char *) outBuff, strlen(outBuff));
	}
	va_end(args);
}

#if defined(ENABLE_WIFI)
void PrintTcp(const char *format, ...)
{
	char outBuff[512];
	va_list args;

	va_start( args, format );

	vsprintf( outBuff, format, args );

	send(TelnetClientSocket, outBuff, strlen(outBuff), 0);
	
	va_end(args);
}

void CloseTelnetConnection(void)
{
	if(TcpConnected)
	{
		usleep(100000);
		TcpConnected = 0;
		close(TelnetClientSocket);
		usleep(100000);
		TelnetClientSocket = -1;
	}
}
#endif	// #if defined(ENABLE_WIFI)

char *get_current_time_str(void)
{
	static char time_str[40];
	time_t esptime = time(0);
	struct tm *tm_time;
	(void)esptime;

	tm_time = localtime(&esptime);

	sprintf(time_str, "%4d/%02d/%02d %02d:%02d:%02d", 
			tm_time->tm_year+1900, tm_time->tm_mon+1, tm_time->tm_mday,
			tm_time->tm_hour, tm_time->tm_min, tm_time->tm_sec);

	return time_str;
}

static void print_time(void)
{
	LOGI("Current time : %s", get_current_time_str());
}

static void set_new_time(char **token)
{
	time_t newtime;
	struct tm _time;
	struct timeval now;
	int year, month, day, hour, min, sec;

	year = atoi(token[1]);
	month = atoi(token[2]);
	day = atoi(token[3]);
	hour = atoi(token[4]);
	min = atoi(token[5]);
	sec = atoi(token[6]);
	
	LOGI("Set new time : %4d/%02d/%02d %02d:%02d:%02d", year, month, day, hour, min, sec);

	_time.tm_year = year - 1900;
    _time.tm_mon = month - 1;
    _time.tm_mday = day;
    _time.tm_hour = hour;
    _time.tm_min = min;
    _time.tm_sec = sec;

	newtime = mktime(&_time);

	now.tv_sec = newtime;
	now.tv_usec = 0;
	settimeofday(&now, NULL);

	print_time();
}

#define MAX_JSON_STRING		2048

void CommandProcess(char *cmd_str)
{
	static bool json_in_process;
	static char json_string[MAX_JSON_STRING + 1];
	const char *delimiters = " \r\n";
	char *token[10];
	int i, token_count, len, json_result;
	
	if(json_in_process)
	{
		len = strlen(cmd_str);
		
		if(cmd_str[len - 1] == 0x04 || cmd_str[len - 1] == '}')	// CTRL-D (End Of Transmission)
		{
			if(cmd_str[len - 1] == 0x04)
			{
				cmd_str[len - 1] = 0;
			}
			strcat(json_string, cmd_str);
			json_result = json_parsing(json_string);
			json_in_process = false;

			if(json_result >= 0)
			{
				if(json_result == 2)
				{
					LOGI("Send OTA start semaphore");
					give_ota_semaphore();
				}
				else
				{
					send_json_info();
				}
			}
			else
			{
				LOGE("JSON parsing ERROR");
			}
		}
		else
		{
			strcat(json_string, cmd_str);
		}
		return;
	}
	
	if(cmd_str[0] == '{')
	{
		json_in_process = true;
		memset(json_string, 0, sizeof(json_string));
		strcpy(json_string, cmd_str);
		len = strlen(cmd_str);

		if(cmd_str[len - 1] == 0x04 || cmd_str[len - 1] == '}')	// CTRL-D (End Of Transmission)
		{
			if(cmd_str[len - 1] == 0x04)
			{
				json_string[len - 1] = 0;
			}
			// LOGI("Json string :\n%s", json_string);
			json_result = json_parsing(json_string);
			json_in_process = false;

			if(json_result >= 0)
			{
				if(json_result == 2)
				{
					LOGI("Send OTA start semaphore");
					give_ota_semaphore();
				}
				else
				{
					// set_led_state(LED_STATE_TRANSMIT_BLINK);
					send_json_info();
					// set_led_state(LED_STATE_DEFAULT_BLINK);
				}
			}
			else
			{
				LOGE("JSON parsing ERROR");
			}
		}
		return;
	}

	LOGI("command string : %s\r\n", cmd_str);
	token[0] = strtok(cmd_str, delimiters);
	if(token[0] == NULL)
	{
		LOGI("No command");
		return;
	}

	for(i=0;i<strlen(token[0]);i++)
	{
		token[0][i] = tolower(token[0][i]);
	}

	for(i=1;i<10;i++)
	{
		token[i] = strtok(NULL, delimiters);
		if(token[i] == NULL) break;
	}

	token_count = i;
	
	if(strcmp(token[0], CMD_REBOOT) == 0)
	{
		esp_restart();
	}
	else if(strcmp(token[0], CMD_TIME) == 0)
	{
		if(token_count == 7) set_new_time(token);
		else if(token_count == 1) print_time();
		else
		{
			LOGI("Invalid time command format");
		}
	}
	else
	{
		LOGW("Unknown command : %s", token[0]);
	}
}

#if defined(ENABLE_WIFI)
static void TaskTcpConsole(void *arg)
{
	int TelnetSocket;
	struct sockaddr_in TelnetAddr, TelnetClientAddr;
	char Buf[256], RcvBuf[512];
	int AddrSize, RcvLen;
	int i, TotalRcvLen;

	LOGI("TCP console started\r\n");
		
	while(1)
	{
		TelnetSocket = socket(PF_INET, SOCK_STREAM, 0);
		if(TelnetSocket == -1)
		{
			ESP_LOGE(TAG, "Telnet debug socket open ERROR!!!");
			usleep(1000000);
		}
		else
		{
			ESP_LOGV(TAG, "Telnet debug socket open : %d",TelnetSocket);
			break;
		}
	}

	memset(&TelnetAddr, 0, sizeof(struct sockaddr_in));
	TelnetAddr.sin_family = AF_INET;
	TelnetAddr.sin_addr.s_addr = INADDR_ANY;
	TelnetAddr.sin_port = htons(23);

	while(1)
	{
		if(bind(TelnetSocket, 
		   (struct sockaddr *)&TelnetAddr, 
		   sizeof(struct sockaddr_in)) != 0)
		{
			ESP_LOGE(TAG, "Telnet socket bind() ERROR!!!");
			usleep(1000000);
		}
		else
		{
			ESP_LOGV(TAG, "Telnet socket bind() OKAY : %d",23);
			break;
		}
	}

	while(1)
	{
		if(listen(TelnetSocket, 1) == -1)
		{
			ESP_LOGE(TAG, "Telnet server listen ERROR!!!");
			usleep(1000000);
			continue;
		}

		ESP_LOGV(TAG, "Telnet server listen() OKAY");
		break;
	}

	while(1)
	{
		AddrSize = sizeof(struct sockaddr_in);

		TelnetClientSocket = accept(TelnetSocket, (struct sockaddr*)&TelnetClientAddr, (socklen_t *)&AddrSize);
		if(TelnetClientSocket == -1)
		{
			ESP_LOGE(TAG, "TCP update server accept ERROR!!!");
			usleep(100000);
			continue;
		}

		ESP_LOGV(TAG, "+++ Telnet server : client connected +++");
		TcpConnected = 1;

		TotalRcvLen = 0;

		PrintTcp("Press 'q' to disconnect\r\n\r\n>");

		while(1)
		{
			RcvLen = recv(TelnetClientSocket, Buf, 256, 0);

			if(RcvLen > 2)
			{
				if((unsigned char)Buf[0] == 0xFF && ((unsigned char)Buf[1] & 0xF0) == 0xF0)
				{
					continue;
				}
			}
			
			if(RcvLen > 0)
			{
				for(i=0;i<RcvLen;i++)
				{
					if(Buf[i] == '\r' || Buf[i] == '\n')
					{
						Buf[i] = 0;
						if(i == 0)
						{
							PrintTcp("\r\n>");
						}
						break;
					}
				}
				if(i != RcvLen)
				{
					strcpy(&RcvBuf[TotalRcvLen], Buf);
					TotalRcvLen += strlen(Buf);

					if(strcmp(RcvBuf, "q") == 0)
					{
						PrintTcp("\r\n--- Bye ---\r\n");
						CloseTelnetConnection();
						break;
					}
					else
					{
						RcvBuf[TotalRcvLen] = 0;
						CommandProcess(RcvBuf);
					}

					TotalRcvLen = 0;
				}
				else
				{
					if((TotalRcvLen + RcvLen) < sizeof(RcvBuf))
					{
						memcpy(&RcvBuf[TotalRcvLen], Buf, RcvLen);
						TotalRcvLen += RcvLen;
					}
					else
					{
						// buffer overflow
						TotalRcvLen = 0;
					}
				}
			}
			else
			{
//				LOGI("--- TCP connection closed ---\r\n",errno(TelnetClientSocket));
				CloseTelnetConnection();
				break;
			}
		}
	}
	
	vTaskDelete(NULL);
}
#endif	// #if defined(ENABLE_WIFI)

static void TaskConsole(void *arg)
{
	static uint8_t data[1024];
	uint8_t rcv_byte;
	int rcv_len;

	LOGI("UART console started\r\n");

	while(1)
	{
		PrintConsole("\r\n>");
		rcv_len = 0;
		
		while(1)
		{
			// Read data from the UART
	        int len = uart_read_bytes(UART_NUM_0, &rcv_byte, 1, portMAX_DELAY);

			if(len != 1)
			{
				ESP_LOGE(TAG, "UART receive error : %d", len);
				rcv_len = 0;
				continue;
			}
#if defined(ENABLE_WIFI)
			if(TcpConnected) continue;
#endif	// #if defined(ENABLE_WIFI)
			
			if(rcv_byte == '\b')
			{
				if(rcv_len <= 0) continue;
				
				rcv_len--;
				uart_write_bytes(UART_NUM_0, "\b \b", 3);
				continue;
			}
			else if(rcv_byte == '\r' || rcv_byte == '\n' || rcv_byte == 0)
			{
				uart_write_bytes(UART_NUM_0, (const char *) &rcv_byte, len);
				rcv_byte = 0;
			}
			else uart_write_bytes(UART_NUM_0, (const char *) &rcv_byte, len);
			
			data[rcv_len] = rcv_byte;
			rcv_len += len;

			// avoid buffer overflow
			if(rcv_len >= (1024 - 1)) rcv_byte = 0;

			if(rcv_byte == 0)
			{
				//ESP_LOGV(TAG, "Receive string : %s", (char *)data);

				if(strcmp((char *)data, "q") == 0)
				{
					break;
				}
				if(data[0] == 0)
				{
					PrintConsole("\r\n>");
				}

				CommandProcess((char *)data);
				
				rcv_len = 0;
			}
		}
	}

	uart_driver_delete(UART_NUM_0);
	vTaskDelete(NULL);
}

static void InitUartConsole(void)
{
	TaskHandle_t handle;
	int ret;

	ret = xTaskCreatePinnedToCore(&TaskConsole, "UART console",
            4096, 
            NULL,
            5,
            &handle,
            tskNO_AFFINITY /*PRO_CPU_NUM*/);
    if (ret != pdPASS) {
		ESP_LOGE(TAG, "ERROR : CAN'T creat task");
        return;
    }
}

#if defined(ENABLE_WIFI)
static void InitTcpConsole(void)
{
	TaskHandle_t handle;

	int ret = xTaskCreatePinnedToCore(&TaskTcpConsole, "TCP console",
            4096, 
            NULL,
            5,
            &handle,
            tskNO_AFFINITY /*PRO_CPU_NUM*/);
    if (ret != pdPASS) {
		ESP_LOGE(TAG, "ERROR : CAN'T creat task");
        return;
    }
}
#endif	// #if defined(ENABLE_WIFI)

void InitDebug(void)
{
	InitUartConsole();
#if defined(ENABLE_WIFI)
	if(is_wifi_enabled())
	{
		InitTcpConsole();
	}
#endif	// #if defined(ENABLE_WIFI)
}
























