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
#include "driver/chip/hal_prcm.h"

#include "camera_sensor_x.h"
#include "http_server.h"
#include "mm_i2s.h"
#include "mimamori.h"

#include "common/framework/sysinfo.h"

int ap_mode = 0;

//char *ssid = "SmartHome-Next";
//char *pwd = "1234qq1234";
char *ssid = "S15303 2947";
char *psk = "1?F17z72";
//连接WiFi现在 - 使用配置的信息
unsigned char connectByConfig(void){

	/* switch to sta mode */
	net_switch_mode(WLAN_MODE_STA);
	/* set ssid and password to wlan */
	wlan_sta_set((uint8_t *)ssid, strlen(ssid), (uint8_t *)psk);
	/* start scan and connect to ap automatically */
	wlan_sta_enable();

	return 0;
}

char * ap_ssid = "Mimamori_CAMERA";
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

MIMAMORI_t mimamori;
int main(void)
{
	char *model = "xr872_mimamori";
	char *version = "1.0";
	mimamori.model = model;
	mimamori.version = version;

	int down_ctr = 0;

	int stime = 3;
	printf("\n\nMimamori v1.1\n\n");
	platform_init();		
	wlan_ap_disable();

	printf("Sleep %d sec ...", stime); OS_Sleep(stime); printf(" done\n");

#if 0
#ifdef  __CONFIG_WLAN_STA
	printf("__CONFIG_WLAN_STA\n");
#endif
#ifdef  __CONFIG_WLAN_AP
	printf("__CONFIG_WLAN_AP\n");
#endif
#ifdef  __CONFIG_WLAN_MONITOR
	printf("__CONFIG_WLAN_MONITOR\n");
#endif
#endif
	sysinfo_load();
	struct sysinfo *si = sysinfo_get();
	if (si) {
		ssid = (char*) si->wlan_sta_param.ssid;
		if (strlen(ssid) > 0) printf("previous ssid: %s\n", ssid);
		psk = (char*) si->wlan_sta_param.psk;
		//printf("g_sysinfo.sta.psk: %s\n\n", si->wlan_sta_param.psk);
		uint8_t *p_ap_ssid = si->wlan_ap_param.ssid;
		uint8_t *p_ap_psk = si->wlan_ap_param.psk;
		if (strlen((char*)si->wlan_ap_param.ssid) == 0) {
			memset(p_ap_ssid, 0, SYSINFO_SSID_LEN_MAX);
			memset(p_ap_psk, 0, SYSINFO_PSK_LEN_MAX);
			memcpy(p_ap_ssid, ap_ssid, strlen(ap_ssid));
			memcpy(p_ap_psk, ap_psk, strlen(ap_psk));
			sysinfo_save();
		} else {
			ap_ssid = (char*) p_ap_ssid;
			ap_psk = (char*) p_ap_psk;
		}
	};

	initCameraSensor(NULL);
	OS_Sleep(3);
	mm_init_i2s(); printf("mm_init_i2s()\n");
	OS_Sleep(3);
	initHttpServer(NULL);
	OS_Sleep(2);
	if (ap_mode) {
		startAP();
	} else {
		connectByConfig();
	}

	while (1) {	
		struct netif *nif = g_wlan_netif;
		if (netif_is_link_up(nif)) {
			down_ctr = 0;
		} else {
			down_ctr++;
		}
		if (down_ctr > 20) {
			printf("link down ctr:%d\n", down_ctr);
			down_ctr = 0;
			//HAL_PRCM_SetCPUABootFlag(PRCM_CPUA_BOOT_FROM_COLD_RESET);
			//HAL_WDG_Reboot();
			ap_mode = 1;
			wlan_sta_disable();
			startAP();
		}
		OS_Sleep(1);
		//printf("link_up:%d, addr:0x%x\n", netif_is_link_up(nif), nif->ip_addr.addr);
		//
		//OS_Sleep(60);
	}

	return 0;

}
