#include "common/framework/platform_init.h"
#include <stdio.h>
#include "kernel/os/os.h"

#include "pm/pm.h"
#include "net/wlan/wlan.h"
#include "net/wlan/wlan_defs.h"
#include "common/framework/net_ctrl.h"
#include "common/framework/platform_init.h"
#include "lwip/inet.h"
#include "driver/chip/hal_wakeup.h"
#include "driver/chip/hal_wdg.h"

#include "camera_sensor_x.h"
#include "http_server.h"
#include "tcp_server.h"

#include "mimamori.h"

//char *ssid = "SmartHome-Next";
//char *pwd = "1234qq1234";
char *ssid = "S15303 2947";
char *pwd = "1?F17z72";
//连接WiFi现在 - 使用配置的信息
unsigned char connectByConfig(void){

	/* switch to sta mode */
	net_switch_mode(WLAN_MODE_STA);
	/* set ssid and password to wlan */
	wlan_sta_set((uint8_t *)ssid, strlen(ssid), (uint8_t *)pwd);	
	/* start scan and connect to ap automatically */
	wlan_sta_enable();

	return 0;
}

char * ap_ssid = "WeiLan_CAMERA";
char * ap_psk = "12345678";
unsigned char startAP(void)
{
	/* switch to ap mode */
	net_switch_mode(WLAN_MODE_HOSTAP);
	/* disable AP to set params*/
	wlan_ap_disable();
	/* set ap's ssid and password */
	wlan_ap_set((uint8_t *)ap_ssid, strlen(ap_ssid), (uint8_t *)ap_psk);
	/* enable ap mode again */
	wlan_ap_enable();
	return 0;
}

//static int cc = 0;
#define TIMER
#ifdef TIMER
OS_TimerCallback_t timercallback(void *arg)
{
	OS_Status ret;
	private_t *p = (private_t*) arg;
	ret = OS_MutexLock(&p->mu, 5000);
	if (ret != OS_OK) {
		printf("OS_MutexLock timeout\n");
	}
	p->do_capture = 1;
	if (ret != OS_OK) {
		printf("OS_MutexUnlock timeout\n");
	}
	ret = OS_MutexUnlock(&p->mu);
	//printf("cb: %d\n", p->count++);
	p->count++;
	return NULL;
}
#endif

int main(void)
{
#ifdef TIMER
	OS_Status ret = 0;
	OS_Timer_t timer_id;
#endif
	private_t private;
	//unsigned int status = 0;
	platform_init();		

	printf("XR872 init\r\n");
	memset(&private, 0, sizeof(private));
	ret = OS_MutexCreate(&private.mu);
	if (ret != OS_OK) {
		printf("Failed: OS_MutexCreate mu\n");
		return (1);
	}
	ret = OS_MutexCreate(&private.jpeg_mu);
	if (ret != OS_OK) {
		printf("Failed: OS_MutexCreate jpeg_mu\n");
		return (1);
	}
	connectByConfig();
	//startAP();
	OS_Sleep(2);
	initCameraSensor(&private);
	OS_Sleep(2);
	initHttpServer();
	OS_Sleep(2);
	initTcpServer();
	
#ifdef TIMER
	OS_TimerSetInvalid(&timer_id);
	ret = OS_TimerCreate(&timer_id, OS_TIMER_PERIODIC,
			(OS_TimerCallback_t) timercallback, &private, 100);
	if (ret != OS_OK) {
		printf("Failed: OS_TimerCreate\n");
	}
	ret = OS_TimerStart(&timer_id);
	if (ret) {
		printf("Failed: OS_TimerStart\n");
	}
#endif

	while (1) {	
		//status++;
		//uint32_t time = OS_TicksToMSecs(OS_GetTicks());
		//getCameraStatus(&private);
		//printf("%d:%08u Quality:%u, Max_cost:%u, Max_size:%u\n",
				//status,
				//time,
				//private.quality,
				//private.max_cost,
				//private.max_size
		      //);
		//printf("%d  CameraRate:%d \r\n",status,getCameraFrameCount());
		OS_Sleep(60);
	}

#ifdef TIMER
	ret = OS_TimerStop(&timer_id);
	ret = OS_TimerDelete(&timer_id);
	if (ret) {
		printf("Failed: OS_TimerStart\n");
	}
#endif

	return 0;

}
