#if !defined (__WIFI_H__)

#define __WIFI_H__

#define ENABLE_WIFI
// #define DISABLE_WIFI

char *get_my_ip(void);
void wifi_init_sta(void);
void wifi_init_softap(void);

#endif  // #if !defined (__WIFI_H__)