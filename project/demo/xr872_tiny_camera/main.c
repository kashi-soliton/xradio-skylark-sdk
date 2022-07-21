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
#include "common/cmd/cmd.h"
#include "common/cmd/cmd_upgrade.h"

int ap_mode = 1;

MIMAMORI_t mimamori;
int main(void)
{
	char *model = "xr872_mimamori";
	char *version = "1.0";
	struct sysinfo *si = NULL;
	mimamori.model = model;
	mimamori.version = version;

	OS_Sleep(1);
	printf("\n\nMimamori v1.5\n\n");
	platform_init();		
	si = sysinfo_get();
	if (si == NULL) {
		printf("Failed to get sysinfo\n");
		OS_Sleep(3);
		return 0;
	}
	printf("sysinfo->wlan_mode: %s\n", si->wlan_mode? "AP": "STA");

	char *ssid = (char*) si->wlan_sta_param.ssid;
	char *psk = (char*) si->wlan_sta_param.psk;
	if (si->wlan_mode == WLAN_MODE_STA) {
		if (strlen(ssid) > 0) {
			printf("previous ssid: %s\n", ssid);
			wlan_sta_set((uint8_t *)ssid, strlen(ssid), (uint8_t *)psk);
			wlan_sta_enable();
		}
	} else {
		si->wlan_mode = WLAN_MODE_STA;
		sysinfo_save();
	}

	OS_Sleep(3);
	initCameraSensor(NULL);
	OS_Sleep(3);
	mm_init_i2s(); printf("mm_init_i2s()\n");
	OS_Sleep(3);
	initHttpServer(NULL);
	OS_Sleep(2);

	struct netif *nif = g_wlan_netif;
	int wlan_mode = 0, link_up = 0, ap_sta_num = 0, ap_sta_ret;
	int down_ctr = 0;
	while (1) {	
		wlan_mode = wlan_if_get_mode(nif);
		link_up = netif_is_link_up(nif);
		ap_sta_ret = wlan_ap_sta_num(&ap_sta_num);
		/*
		printf("mode:%d->%d link_up:%d ret:%d num:%d\n",
				wlan_mode,
				si->wlan_mode, link_up,
				ap_sta_ret, ap_sta_num);
		*/
		if (((wlan_mode == WLAN_MODE_STA) && link_up) ||
				((ap_sta_ret == 0) && (ap_sta_num != 0))) {
			down_ctr = 0;
		} else {
			down_ctr++;
		}
		if (down_ctr > 30) {
			if (wlan_mode == WLAN_MODE_STA) {
				wlan_sta_disable();
				net_switch_mode(WLAN_MODE_HOSTAP);
				wlan_ap_enable();
				down_ctr = 0;
			} else {
				OS_Sleep(1);
				cmd_reboot_exec(NULL);
			}
		}
		mimamori_udpmsg();
		OS_Sleep(1);
	}

	return 0;

}
