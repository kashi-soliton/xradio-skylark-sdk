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

#define TIMER
#ifdef TIMER
OS_TimerCallback_t timercallback(void *arg)
{
	static int tc_count = 0;
	static int trigger_count = 0;
	static int next_tc = 0;
	uint32_t *dms = NULL;
	uint32_t capture = 0;
	OS_Status ret;
	private_t *p = (private_t*) arg;
	
	dms = &p->dms[trigger_count % p->dms_num];
	if (next_tc == 0) next_tc = *dms;
	if (tc_count >= next_tc) {
		capture = 1;
		next_tc = tc_count + *dms;
		trigger_count++;
		//printf("trigger:%d, tc_count:%d perod:%d\n", trigger_count, tc_count, *dms);
	} else {
		capture = 0;
	}

	ret = OS_MutexLock(&p->mu, 5000);
	if (ret != OS_OK) {
		printf("OS_MutexLock timeout\n");
		return NULL;
	}
	p->do_capture = capture;
	ret = OS_MutexUnlock(&p->mu);

	tc_count++;
	return NULL;
}
#endif

int setfps(private_t *private, uint32_t fps)
{
	if (fps == 30) {
		private->dms_num = 3;
		private->dms[0] = 33;
		private->dms[1] = 33;
		private->dms[2] = 34;
	} else if (fps == 15) {
		private->dms_num = 3;
		private->dms[0] = 66;
		private->dms[1] = 67;
		private->dms[2] = 67;
	} else if (fps == 10) {
		private->dms_num = 1;
		private->dms[0] = 100;
	} else {
		printf("Unknown fps %d. Set to 10 fps.\n", fps);
		private->dms_num = 1;
		private->dms[0] = 100;
	}
	return 0;
}

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
	private.jpeg_buf = malloc_jpeg();
	private.jpeg_size = 0;
	ret = OS_MutexCreate(&private.mu);
	if (ret != OS_OK) {
		printf("Failed: OS_MutexCreate mu\n");
		return (1);
	}
	connectByConfig();
	//startAP();
	OS_Sleep(2);
	initCameraSensor(&private);
	OS_Sleep(2);
	initHttpServer(&private);
	OS_Sleep(2);
	initTcpServer(&private);
	
#ifdef TIMER
	setfps(&private, 15);
	OS_TimerSetInvalid(&timer_id);
	ret = OS_TimerCreate(&timer_id, OS_TIMER_PERIODIC,
			(OS_TimerCallback_t) timercallback, &private, 1);
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
	if (ret) {
		printf("Failed: OS_TimerStop\n");
	}
	ret = OS_TimerDelete(&timer_id);
	if (ret) {
		printf("Failed: OS_TimerStop\n");
	}
#endif

	OS_MutexDelete(&private.mu);
	free_jpeg(private.jpeg_buf);

	return 0;

}
