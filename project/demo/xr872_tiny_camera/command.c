/*
 * Copyright (C) 2017 XRADIO TECHNOLOGY CO., LTD. All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *    1. Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *    2. Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the
 *       distribution.
 *    3. Neither the name of XRADIO TECHNOLOGY CO., LTD. nor the names of
 *       its contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *  A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *  OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *  THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#define _GNU_SOURCE

#include "common/cmd/cmd_util.h"
#include "common/cmd/cmd.h"

#include "common/framework/sysinfo.h"


#if PRJCONF_NET_EN

#define COMMAND_IPERF       1
#define COMMAND_PING        1

/*
 * net commands
 */
static enum cmd_status cmd_setwifi_exec(char *cmd)
{
	struct sysinfo *si = sysinfo_get();
	char *ssid, *psk, *tmp;
	//printf( "current ssid:%s\n", si->wlan_sta_param.ssid);
	//printf( "current psk:%s\n", si->wlan_sta_param.psk);
	//printf("cmd:%s\n", cmd);
	ssid = strcasestr(cmd, "S:");
	if (ssid) ssid += 2;
	psk = strcasestr(cmd, "P:");
	if (psk) psk += 2;
	//
	if (ssid) {
		tmp = strstr(ssid, ";");
		if (tmp) *tmp = '\0';
		printf("ssid:%s\n", ssid);
	}
	if (psk) {
		tmp = strstr(psk, ";");
		if (tmp) *tmp = '\0';
		printf("psk:%s\n", psk);
	}
	if (ssid && psk) {
		uint8_t *pssid = si->wlan_sta_param.ssid;
		uint8_t *ppsk = si->wlan_sta_param.psk;
		memset(pssid, 0, SYSINFO_SSID_LEN_MAX);
		memset(ppsk, 0, SYSINFO_PSK_LEN_MAX);
		memcpy(pssid, ssid, strlen(ssid));
		memcpy(ppsk, psk, strlen(psk));
		si->wlan_mode = WLAN_MODE_STA;
		sysinfo_save();
		return CMD_STATUS_OK;
	} else {
		return CMD_STATUS_INVALID_ARG;
	}
}

static const struct cmd_data g_net_cmds[] = {
	{ "mode",		cmd_wlan_mode_exec },
#ifdef __CONFIG_WLAN_AP
	{ "ap", 		cmd_wlan_ap_exec },
#endif
#ifdef __CONFIG_WLAN_STA
	{ "sta",		cmd_wlan_sta_exec },
#endif
	{ "ifconfig",	cmd_ifconfig_exec },
#if COMMAND_IPERF
	{ "iperf",		cmd_iperf_exec },
#endif
#if COMMAND_PING
	{ "ping",		cmd_ping_exec },
#endif
};

static enum cmd_status cmd_net_exec(char *cmd)
{
	return cmd_exec(cmd, g_net_cmds, cmd_nitems(g_net_cmds));
}

#endif /* PRJCONF_NET_EN */

/*
 * main commands
 */
static const struct cmd_data g_main_cmds[] = {
#if PRJCONF_NET_EN
	{ "net",	cmd_net_exec },
#endif
#ifdef __CONFIG_OTA
	{ "ota",	cmd_ota_exec },
#endif
	{ "echo",	cmd_echo_exec },
	{ "mem",	cmd_mem_exec },
	{ "heap",	cmd_heap_exec },
	{ "thread",	cmd_thread_exec },
	{ "upgrade",cmd_upgrade_exec },
	{ "reboot", cmd_reboot_exec },
	//{ "efpg",	cmd_efpg_exec },
	//{ "gpio",	cmd_gpio_exec },
	{ "sysinfo",	cmd_sysinfo_exec },
	//{ "hdcpd",	cmd_dhcpd_exec },
	{ "setwifi",	cmd_setwifi_exec },
};

void main_cmd_exec(char *cmd)
{
	enum cmd_status status;

	if (cmd[0] != '\0') {
#if (!CONSOLE_ECHO_EN)
		if (cmd_strcmp(cmd, "efpg"))
			CMD_LOG(CMD_DBG_ON, "$ %s\n", cmd);
#endif
		status = cmd_exec(cmd, g_main_cmds, cmd_nitems(g_main_cmds));
		if (status != CMD_STATUS_ACKED) {
			cmd_write_respond(status, cmd_get_status_desc(status));
		}
	}
#if (!CONSOLE_ECHO_EN)
	else { /* empty command */
		CMD_LOG(1, "$\n");
	}
#endif
#if CONSOLE_ECHO_EN
	console_write((uint8_t *)"$ ", 2);
#endif
}
