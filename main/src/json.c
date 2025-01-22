/****************************************************************************/
//  File    : json.c
//---------------------------------------------------------------------------
//  Description: 
//             JSON Parser
//  
//  
//  History : 
//---------------------------------------------------------------------------
//   Date      | Author          | Version |  Modification 
//-------------+--------+---------+------------------------------------------
// 1 Sep 2022 |  Kwon Taeyoung   |   1.0   |  Creation
/****************************************************************************/

#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/unistd.h>
#include "mbedtls/base64.h"

#include "esp_system.h"
#include "esp_log.h"
#include "jsmn.h"
#include "debug.h"
#include "ota.h"

/*---------------------------- User define -------------------------------*/
#define TAG	"JSON"

#define MAX_JSON_PACKET_SIZE	(3 * 1024)
#define MAX_JSON_PARSING_TOKEN	32

#define JSON_KEY_DATETIME			"datetime"
#define JSON_KEY_FIRMWARE			"firmware"
#define JSON_KEY_START				"start"
#define JSON_KEY_OTA				"ota"
#define JSON_KEY_OTA_SIZE			"ota size"

#define JSON_VALUE_START			"start"
#define JSON_VALUE_READY			"ready"
#define JSON_VALUE_NOT_READY		"not ready"
#define JSON_VALUE_INVALID_SIZE		"invalid size"

#define JSON_KEY_GROUPS			"groups"

/*---------------------------- Variables ---------------------------------*/
static jsmntok_t json_token[MAX_JSON_PARSING_TOKEN]; /* We expect no more than MAX_JSON_PARSING_TOKEN tokens */
static int ota_start;
static int ota_size;
static int file_transfer_flag;
static int file_transfer_type;	// 1 : compress, 0 : plain
static char transfer_filename[64];
static uint8_t json_packet[MAX_JSON_PACKET_SIZE];
/*-------------------------- Function declares ---------------------------*/

//void base64_encode(uint8_t *in, char *out)
//{
//	mbedtls_base64_encode(unsigned char * dst, size_t dlen, size_t * olen, const unsigned char * src, size_t slen)
//	mbedtls_base64_decode(unsigned char * dst, size_t dlen, size_t * olen, const unsigned char * src, size_t slen)
//}

void send_json_info(void)
{
	struct tm *st_time;
	time_t _time;
	char buf[256];
	
	_time = time(0);
	st_time = localtime(&_time);

	sprintf((char *)json_packet, "{\n\t\""JSON_KEY_DATETIME"\":\"%04d-%02d-%02d %02d:%02d:%02d\"", 
		st_time->tm_year+1900, st_time->tm_mon+1, st_time->tm_mday, st_time->tm_hour, st_time->tm_min, st_time->tm_sec);
	sprintf(buf, ",\n\t\""JSON_KEY_FIRMWARE"\":\"%s\"", get_version_string());
	strcat((char *)json_packet, buf);

	if(is_ota_ready())
	{
		sprintf(buf, ",\n\t\""JSON_KEY_OTA"\":\""JSON_VALUE_READY"\"" );
	}
	else
	{
		if(ota_size < 0 || ota_size > 0x0F0000)
		{
			sprintf(buf, ",\n\t\""JSON_KEY_OTA"\":\""JSON_VALUE_INVALID_SIZE"\"" );
		}
		else
		{
			sprintf(buf, ",\n\t\""JSON_KEY_OTA"\":\""JSON_VALUE_NOT_READY"\"" );
		}
	}
	strcat((char *)json_packet, buf);

	strcat((char *)json_packet, "\n}\x04");

	LOGI("Send message : \n%s", json_packet);

	_nordic_uart_send(json_packet, strlen((char *)json_packet));

	LOGI("JSON send finished");
}

static int jsoneq(const char *json, jsmntok_t *tok, const char *s) 
{
	if (tok->type == JSMN_STRING && (int)strlen(s) == tok->end - tok->start &&
	  strncmp(json + tok->start, s, tok->end - tok->start) == 0) 
	{
		return 0;
	}
	
	return -1;
}

/*******************************************
Return : EVENT_REQUEST 에 대한 응답을 우선 처리함.
         EVENT_REQUEST 가 있을 경우 다른 key 값이 있어도 처리만 하고 응답은 보내지 않음
-1 : Parsing error
0  : Request event data
1  : Set parameters or delete
2  : OTA
*******************************************/
int json_parsing(char *json_string)
{
	int ret = 1;
	int i, r;
	jsmn_parser p;
	char str_value[64];
//	char str_groups[5][100];

	ota_start = 0;
	ota_size = 0;
	file_transfer_flag = 0;
	file_transfer_type = 0;	// default : plain
	memset(transfer_filename, 0, sizeof(transfer_filename));

	jsmn_init(&p);

	r = jsmn_parse(&p, json_string, strlen(json_string), json_token,
                 sizeof(json_token) / sizeof(json_token[0]));
	if (r < 0) 
	{
		LOGI("Failed to parse JSON: %d", r);
		return -1;
	}

	/* Assume the top-level element is an object */
	if (r < 1 || json_token[0].type != JSMN_OBJECT) 
	{
		LOGI("Object expected");
		return -1;
	}

	/* Loop over all keys of the root object */
	for (i = 1; i < r; i++) 
	{
		if (jsoneq(json_string, &json_token[i], JSON_KEY_OTA) == 0) 
		{
			snprintf(str_value, sizeof(str_value), "%.*s", json_token[i + 1].end - json_token[i + 1].start,
					json_string + json_token[i + 1].start);
			if(strcmp(str_value, JSON_VALUE_START) == 0)
			{
				LOGI("Received OTA start command");
				ota_start = 1;
			}
			else
			{
				LOGI("Received OTA key but not 'start' : %s", str_value);
			}

			i++;
		} 
		else if (jsoneq(json_string, &json_token[i], JSON_KEY_OTA_SIZE) == 0) 
		{
			snprintf(str_value, sizeof(str_value), "%.*s", json_token[i + 1].end - json_token[i + 1].start,
					json_string + json_token[i + 1].start);

			ota_size = atoi(str_value);
			if(ota_size > 0)
			{
				LOGI("Received OTA size : %d", ota_size);
			}
			else
			{
				LOGI("Received OTA SIZE key but invalid size : %d (0x%08X)", ota_size, ota_size);
			}

			i++;
		} 
		else if (jsoneq(json_string, &json_token[i], JSON_KEY_GROUPS) == 0) 
		{
			int j;
			LOGI("- Groups:");
			if (json_token[i + 1].type != JSMN_ARRAY) 
			{
				continue; /* We expect groups to be an array of strings */
			}

			for (j = 0; j < json_token[i + 1].size; j++) 
			{
				jsmntok_t *g = &json_token[i + j + 2];
				(void)g;
				LOGI("  * %.*s", g->end - g->start, json_string + g->start);
			}
			i += json_token[i + 1].size + 1;
		} 
		else 
		{
			LOGI("Unexpected key: %.*s", json_token[i].end - json_token[i].start,
			     json_string + json_token[i].start);
		}
	}

	if(is_ota_ready()) ret = 2;
	
	return ret;
}

int is_ota_ready(void)
{
	if(ota_start > 0 && ota_size > 0 && ota_size < MAX_FIRMWARE_SIZE) return 1;

	return 0;
}

int get_ota_file_size(void)
{
	return ota_size;
}

void clear_ota_state(void)
{
	ota_start = 0;
	ota_size = 0;
}

