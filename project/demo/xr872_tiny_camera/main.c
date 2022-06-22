#include "common/framework/platform_init.h"
#include <stdio.h>
#include "kernel/os/os.h"
#include "kernel/os/os_cpuusage.h"

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
#include "mm_i2s.h"

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

int main(void)
{
	int stime = 3;
	printf("\n\nMimamori v1.0\n\n");
	platform_init();		
	printf("Sleep %d sec ...", stime); OS_Sleep(stime); printf(" done\n");

	connectByConfig();
	//startAP();
	OS_Sleep(2);
	mm_init_i2s(); printf("mm_init_i2s()\n");
	OS_Sleep(2);
	initCameraSensor(NULL);
	OS_Sleep(2);
	initHttpServer(NULL);

	while (1) {	
		OS_Sleep(60);
	}

	return 0;

}
