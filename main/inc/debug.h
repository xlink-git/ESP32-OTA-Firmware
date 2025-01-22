/****************************************************************************/
//  File    : debug.h
//---------------------------------------------------------------------------
//  Scope   :  
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

#if !defined (__DEBUG_H__)

#define __DEBUG_H__

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "time.h"

#ifdef __cplusplus
extern "C" {
#endif

/*---------------------------- User define -------------------------------*/
#define ENABLE_LOG		1
#define USE_ESP32_LOG	0

#if(ENABLE_LOG)
	#define MLOG(fmt, args...)	PrintConsole("%s %d "fmt"\r\n", __FILE__, __LINE__, ##args)

	#if(USE_ESP32_LOG)
	#define LOGD(fmt, args...) ESP_LOGD("", "%s %d "fmt, __FILE__, __LINE__, ##args)
	#define LOGI(fmt, args...) ESP_LOGI("", "%s %d "fmt, __FILE__, __LINE__, ##args)
	#define LOGW(fmt, args...) ESP_LOGW("", "%s %d "fmt, __FILE__, __LINE__, ##args)
	#define LOGE(fmt, args...) ESP_LOGE("", "%s %d "fmt, __FILE__, __LINE__, ##args)
	#else
		#if(CONFIG_LOG_DEFAULT_LEVEL >= 4)
		#define LOGD(fmt, args...) PrintConsole("%s %d "fmt"\r\n", __FILE__, __LINE__, ##args)
		#else
		#define LOGD(fmt, args...)
		#endif

		#if(CONFIG_LOG_DEFAULT_LEVEL >= 3)
		#define LOGI(fmt, args...) PrintConsole("%s %d "fmt"\r\n", __FILE__, __LINE__, ##args)
		#else
		#define LOGI(fmt, args...)
		#endif

		#if(CONFIG_LOG_DEFAULT_LEVEL >= 2)
		#define LOGW(fmt, args...) PrintConsole("%s %d "fmt"\r\n", __FILE__, __LINE__, ##args)
		#else
		#define LOGW(fmt, args...)
		#endif

		#if(CONFIG_LOG_DEFAULT_LEVEL >= 1)
		#define LOGE(fmt, args...) PrintConsole("%s %d "fmt"\r\n", __FILE__, __LINE__, ##args)
		#else
		#define LOE(fmt, args...)
		#endif

	#endif
#else
#define MLOG(fmt, args...)
#define LOGD(fmt, args...)
#define LOGI(fmt, args...)
#define LOGW(fmt, args...)
#define LOGE(fmt, args...)
#endif

#define GET_NOW(now)	gettimeofday(&now, NULL);

#define TIME_DIFF_MS(_end, _start)  ((uint32_t)(((_end).tv_sec - (_start).tv_sec) * 1000 + (_end.tv_usec - _start.tv_usec)/1000))
#define TIME_DIFF_SEC(_end, _start) ((uint32_t)((_end).tv_sec - (_start).tv_sec))

/*---------------------------- Variables ---------------------------------*/

/*-------------------------- Function declares ---------------------------*/
void InitDebug(void);

void PrintTcp(const char *format, ...);
void PrintConsole(const char *format, ...);
void CommandProcess(char *cmd_str);
void CloseTelnetConnection(void);
char *get_version_string(void);
char *get_my_ip(void);
char *get_current_time_str(void);

#ifdef __cplusplus
}
#endif

#endif  /* End_of __DEBUG_H */




























