//----------------------------------------------------------------------------//
//#include <flash/stm32_flash.h>
#if !defined(CONFIG_MBED_ENABLED) && !defined(CONFIG_PLATFOMR_CUSTOMER_RTOS)
#include "main.h"
#include <lwip_netconf.h>
#include <lwip/sockets.h>
#include <dhcp/dhcps.h>
#include "tcpip.h"
#endif
#include <platform/platform_stdlib.h>
#include <wifi/wifi_conf.h>
#include <wifi/wifi_util.h>
#include <wifi/wifi_ind.h>
#include <osdep_service.h>
#include <device_lock.h>
#include <wlan_intf.h>

#if CONFIG_EXAMPLE_WLAN_FAST_CONNECT || CONFIG_JD_SMART
#include "wlan_fast_connect/example_wlan_fast_connect.h"
#if defined(CONFIG_FAST_DHCP) && CONFIG_FAST_DHCP
#include "flash_api.h"
#include "device_lock.h"
#endif
#endif
#if (defined(CONFIG_EXAMPLE_UART_ATCMD) && CONFIG_EXAMPLE_UART_ATCMD) || (defined(CONFIG_EXAMPLE_SPI_ATCMD) && CONFIG_EXAMPLE_SPI_ATCMD)
#include "at_cmd/atcmd_wifi.h"
#endif
#if defined(CONFIG_PLATFORM_8710C)
#include "platform_opts_bt.h"
#endif

#if CONFIG_INIC_EN
extern int inic_start(void);
extern int inic_stop(void);
#endif

/******************************************************
 *                    Constants
 ******************************************************/
#define SCAN_USE_SEMAPHORE	0

#define RTW_JOIN_TIMEOUT 20000

#define JOIN_ASSOCIATED             (uint32_t)(1 << 0)
#define JOIN_AUTHENTICATED          (uint32_t)(1 << 1)
#define JOIN_LINK_READY             (uint32_t)(1 << 2)
#define JOIN_SECURITY_COMPLETE      (uint32_t)(1 << 3)
#define JOIN_COMPLETE               (uint32_t)(1 << 4)
#define JOIN_NO_NETWORKS            (uint32_t)(1 << 5)
#define JOIN_WRONG_SECURITY         (uint32_t)(1 << 6)
#define JOIN_HANDSHAKE_DONE         (uint32_t)(1 << 7)
#define JOIN_SIMPLE_CONFIG          (uint32_t)(1 << 8)
#define JOIN_AIRKISS                (uint32_t)(1 << 9)
#define JOIN_CONNECTING             (uint32_t)(1 << 10)

/******************************************************
 *                 Type Definitions
 ******************************************************/

/******************************************************
 *               Variables Declarations
 ******************************************************/

#if !defined(CONFIG_MBED_ENABLED)
extern struct netif xnetif[NET_IF_NUM];
#endif

/******************************************************
 *               Variables Definitions
 ******************************************************/
static internal_scan_handler_t scan_result_handler_ptr = {0, 0, 0, RTW_FALSE, 0, 0, 0, 0, 0};
static internal_join_result_t *join_user_data;
static unsigned char ap_bssid[ETH_ALEN];
#if CONFIG_WIFI_IND_USE_THREAD
static void *disconnect_sema = NULL;
#endif
#if defined(CONFIG_MBED_ENABLED)
rtw_mode_t wifi_mode = RTW_MODE_STA;
#endif
extern rtw_mode_t wifi_mode;
static rtw_wpa_mode wifi_wpa_mode = WPA_AUTO_MODE;

int error_flag = RTW_UNKNOWN;
uint32_t rtw_join_status;
#if ATCMD_VER == ATVER_2
extern unsigned char dhcp_mode_sta;
#endif
/* The flag to check if wifi init is completed */
static int _wifi_is_on = 0;
void *param_indicator;
struct task_struct wifi_autoreconnect_task;
/******************************************************
 *               Variables Definitions
 ******************************************************/

#ifndef WLAN0_NAME
#define WLAN0_NAME		"wlan0"
#endif
#ifndef WLAN1_NAME
#define WLAN1_NAME		"wlan1"
#endif

const char *scan_interface = WLAN0_NAME;

/* Give default value if not defined */
#ifndef NET_IF_NUM
#ifdef CONFIG_CONCURRENT_MODE
#define NET_IF_NUM 2
#else
#define NET_IF_NUM 1
#endif
#endif

/*Static IP ADDRESS*/
#ifndef IP_ADDR0
#define IP_ADDR0   192
#define IP_ADDR1   168
#define IP_ADDR2   1
#define IP_ADDR3   80
#endif

/*NETMASK*/
#ifndef NETMASK_ADDR0
#define NETMASK_ADDR0   255
#define NETMASK_ADDR1   255
#define NETMASK_ADDR2   255
#define NETMASK_ADDR3   0
#endif

/*Gateway Address*/
#ifndef GW_ADDR0
#define GW_ADDR0   192
#define GW_ADDR1   168
#define GW_ADDR2   1
#define GW_ADDR3   1
#endif

/*Static IP ADDRESS*/
#ifndef AP_IP_ADDR0
#define AP_IP_ADDR0   192
#define AP_IP_ADDR1   168
#define AP_IP_ADDR2   43
#define AP_IP_ADDR3   1
#endif

/*NETMASK*/
#ifndef AP_NETMASK_ADDR0
#define AP_NETMASK_ADDR0   255
#define AP_NETMASK_ADDR1   255
#define AP_NETMASK_ADDR2   255
#define AP_NETMASK_ADDR3   0
#endif

/*Gateway Address*/
#ifndef AP_GW_ADDR0
#define AP_GW_ADDR0   192
#define AP_GW_ADDR1   168
#define AP_GW_ADDR2   43
#define AP_GW_ADDR3   1
#endif

#if defined(CONFIG_AUTO_RECONNECT) && CONFIG_AUTO_RECONNECT
#ifndef AUTO_RECONNECT_COUNT
#define AUTO_RECONNECT_COUNT	8
#endif
#ifndef AUTO_RECONNECT_INTERVAL
#define AUTO_RECONNECT_INTERVAL	5	// in sec
#endif
#endif

/******************************************************
 *               Function Definitions
 ******************************************************/

#if CONFIG_WLAN

#ifdef CONFIG_ENABLE_EAP
extern int get_eap_phase(void);
#endif

extern unsigned char is_promisc_enabled(void);
extern int promisc_set(rtw_rcr_level_t enabled, void (*callback)(unsigned char *, unsigned int, void *), unsigned char len_used);
extern unsigned char _is_promisc_enabled(void);
extern int wext_get_drv_ability(const char *ifname, __u32 *ability);
extern void rltk_wlan_btcoex_set_bt_state(unsigned char state);
extern int wext_close_lps_rf(const char *ifname);
extern int rltk_wlan_reinit_drv_sw(const char *ifname, rtw_mode_t mode);
extern int rltk_set_mode_prehandle(rtw_mode_t curr_mode, rtw_mode_t next_mode, const char *ifname);
extern int rltk_set_mode_posthandle(rtw_mode_t curr_mode, rtw_mode_t next_mode, const char *ifname);
#ifdef CONFIG_PMKSA_CACHING
extern int wifi_set_pmk_cache_enable(unsigned char value);
#endif

//----------------------------------------------------------------------------//
static int wifi_connect_local(rtw_network_info_t *pWifi)
{
	int ret = 0;

	if (is_promisc_enabled()) {
		promisc_set(RTW_PROMISC_DISABLE, NULL, 0);
	}

	/* lock 4s to forbid suspend under linking */
	rtw_wakelock_timeout(4 * 1000);

	if (!pWifi) {
		return -1;
	}
	switch (pWifi->security_type) {
	case RTW_SECURITY_OPEN:
		ret = wext_set_key_ext(WLAN0_NAME, IW_ENCODE_ALG_NONE, NULL, 0, 0, 0, 0, NULL, 0);
		break;
	case RTW_SECURITY_WEP_PSK:
	case RTW_SECURITY_WEP_SHARED:
		ret = wext_set_auth_param(WLAN0_NAME, IW_AUTH_80211_AUTH_ALG, IW_AUTH_ALG_SHARED_KEY);
		if (ret == 0) {
			ret = wext_set_key_ext(WLAN0_NAME, IW_ENCODE_ALG_WEP, NULL, pWifi->key_id, 1 /* set tx key */, 0, 0, pWifi->password, pWifi->password_len);
		}
		break;
	case RTW_SECURITY_WPA_TKIP_PSK:
	case RTW_SECURITY_WPA2_TKIP_PSK:
	case RTW_SECURITY_WPA_WPA2_TKIP_PSK:
		ret = wext_set_auth_param(WLAN0_NAME, IW_AUTH_80211_AUTH_ALG, IW_AUTH_ALG_OPEN_SYSTEM);
		if (ret == 0) {
			ret = wext_set_key_ext(WLAN0_NAME, IW_ENCODE_ALG_TKIP, NULL, 0, 0, 0, 0, NULL, 0);
		}
		if (ret == 0) {
			ret = wext_set_passphrase(WLAN0_NAME, pWifi->password, pWifi->password_len);
		}
		if (ret == 0) {
			ret = rltk_wlan_set_wpa_mode(WLAN0_NAME, wifi_wpa_mode);
		}
		break;
	case RTW_SECURITY_WPA_AES_PSK:
	case RTW_SECURITY_WPA_MIXED_PSK:
	case RTW_SECURITY_WPA2_AES_PSK:
	case RTW_SECURITY_WPA2_MIXED_PSK:
	case RTW_SECURITY_WPA_WPA2_AES_PSK:
	case RTW_SECURITY_WPA_WPA2_MIXED_PSK:
#ifdef CONFIG_SAE_SUPPORT
	case RTW_SECURITY_WPA3_AES_PSK:
	case RTW_SECURITY_WPA2_WPA3_MIXED:
#endif
		ret = wext_set_auth_param(WLAN0_NAME, IW_AUTH_80211_AUTH_ALG, IW_AUTH_ALG_OPEN_SYSTEM);
		if (ret == 0) {
			ret = wext_set_key_ext(WLAN0_NAME, IW_ENCODE_ALG_CCMP, NULL, 0, 0, 0, 0, NULL, 0);
		}
		if (ret == 0) {
			ret = wext_set_passphrase(WLAN0_NAME, pWifi->password, pWifi->password_len);
		}
		if (ret == 0) {
			ret = rltk_wlan_set_wpa_mode(WLAN0_NAME, wifi_wpa_mode);
		}
		break;
	default:
		ret = -1;
		RTW_API_INFO("\n\rWIFICONF: security type(0x%x) is not supported.\n\r", pWifi->security_type);
		break;
	}
	if (ret == 0) {
		ret = wext_set_ssid(WLAN0_NAME, pWifi->ssid.val, pWifi->ssid.len);
	}
	return ret;
}

static int wifi_connect_bssid_local(rtw_network_info_t *pWifi)
{
	int ret = 0;
	u8 bssid[12] = {0};

	if (is_promisc_enabled()) {
		promisc_set(RTW_PROMISC_DISABLE, NULL, 0);
	}

	/* lock 4s to forbid suspend under linking */
	rtw_wakelock_timeout(4 * 1000);

	if (!pWifi) {
		return -1;
	}
	switch (pWifi->security_type) {
	case RTW_SECURITY_OPEN:
		ret = wext_set_key_ext(WLAN0_NAME, IW_ENCODE_ALG_NONE, NULL, 0, 0, 0, 0, NULL, 0);
		break;
	case RTW_SECURITY_WEP_PSK:
	case RTW_SECURITY_WEP_SHARED:
		ret = wext_set_auth_param(WLAN0_NAME, IW_AUTH_80211_AUTH_ALG, IW_AUTH_ALG_SHARED_KEY);
		if (ret == 0) {
			ret = wext_set_key_ext(WLAN0_NAME, IW_ENCODE_ALG_WEP, NULL, pWifi->key_id, 1 /* set tx key */, 0, 0, pWifi->password, pWifi->password_len);
		}
		break;
	case RTW_SECURITY_WPA_TKIP_PSK:
	case RTW_SECURITY_WPA2_TKIP_PSK:
	case RTW_SECURITY_WPA_WPA2_TKIP_PSK:
		ret = wext_set_auth_param(WLAN0_NAME, IW_AUTH_80211_AUTH_ALG, IW_AUTH_ALG_OPEN_SYSTEM);
		if (ret == 0) {
			ret = wext_set_key_ext(WLAN0_NAME, IW_ENCODE_ALG_TKIP, NULL, 0, 0, 0, 0, NULL, 0);
		}
		if (ret == 0) {
			ret = wext_set_passphrase(WLAN0_NAME, pWifi->password, pWifi->password_len);
		}
		if (ret == 0) {
			ret = rltk_wlan_set_wpa_mode(WLAN0_NAME, wifi_wpa_mode);
		}
		break;
	case RTW_SECURITY_WPA_AES_PSK:
	case RTW_SECURITY_WPA_MIXED_PSK:
	case RTW_SECURITY_WPA2_AES_PSK:
	case RTW_SECURITY_WPA2_MIXED_PSK:
	case RTW_SECURITY_WPA_WPA2_AES_PSK:
	case RTW_SECURITY_WPA_WPA2_MIXED_PSK:
#ifdef CONFIG_SAE_SUPPORT
	case RTW_SECURITY_WPA3_AES_PSK:
	case RTW_SECURITY_WPA2_WPA3_MIXED:
#endif
		ret = wext_set_auth_param(WLAN0_NAME, IW_AUTH_80211_AUTH_ALG, IW_AUTH_ALG_OPEN_SYSTEM);
		if (ret == 0) {
			ret = wext_set_key_ext(WLAN0_NAME, IW_ENCODE_ALG_CCMP, NULL, 0, 0, 0, 0, NULL, 0);
		}
		if (ret == 0) {
			ret = wext_set_passphrase(WLAN0_NAME, pWifi->password, pWifi->password_len);
		}
		if (ret == 0) {
			ret = rltk_wlan_set_wpa_mode(WLAN0_NAME, wifi_wpa_mode);
		}
		break;
	default:
		ret = -1;
		RTW_API_INFO("\n\rWIFICONF: security type(0x%x) is not supported.\n\r", pWifi->security_type);
		break;
	}
	if (ret == 0) {
		memcpy(bssid, pWifi->bssid.octet, ETH_ALEN);
		if (pWifi->ssid.len) {
			bssid[ETH_ALEN] = '#';
			bssid[ETH_ALEN + 1] = '@';
			memcpy(bssid + ETH_ALEN + 2, &pWifi, sizeof(pWifi));
		}
		ret = wext_set_bssid(WLAN0_NAME, bssid);
	}
	return ret;
}

void wifi_rx_beacon_hdl(char *buf, int buf_len, int flags, void *userdata)
{
	/* To avoid gcc warnings */
	(void) buf;
	(void) buf_len;
	(void) flags;
	(void) userdata;

	//RTW_API_INFO("Beacon!\n");
}


static void wifi_no_network_hdl(char *buf, int buf_len, int flags, void *userdata)
{
	/* To avoid gcc warnings */
	(void) buf;
	(void) buf_len;
	(void) flags;
	(void) userdata;

	if (join_user_data != NULL) {
		rtw_join_status = JOIN_NO_NETWORKS | JOIN_CONNECTING;
	}
}

static void wifi_connected_hdl(char *buf, int buf_len, int flags, void *userdata)
{
	/* To avoid gcc warnings */
	(void) buf_len;
	(void) flags;
	(void) userdata;
	/* buf detail: ap bssid, buf_len = ETH_ALEN*/
	rtw_memcpy(ap_bssid, buf, ETH_ALEN);

#ifdef CONFIG_ENABLE_EAP
	if (get_eap_phase()) {
		rtw_join_status = JOIN_COMPLETE | JOIN_SECURITY_COMPLETE | JOIN_ASSOCIATED | JOIN_AUTHENTICATED | JOIN_LINK_READY | JOIN_CONNECTING;
		return;
	}
#endif /* CONFIG_ENABLE_EAP */

	if ((join_user_data != NULL) && ((join_user_data->network_info.security_type == RTW_SECURITY_OPEN) ||
									 (join_user_data->network_info.security_type == RTW_SECURITY_WEP_PSK) ||
									 (join_user_data->network_info.security_type == RTW_SECURITY_WEP_SHARED))) {
		rtw_join_status = JOIN_COMPLETE | JOIN_SECURITY_COMPLETE | JOIN_ASSOCIATED | JOIN_AUTHENTICATED | JOIN_LINK_READY | JOIN_CONNECTING;
		rtw_up_sema(&join_user_data->join_sema);
	} else if ((join_user_data != NULL) && ((join_user_data->network_info.security_type == RTW_SECURITY_WPA2_AES_PSK) ||
											(join_user_data->network_info.security_type == RTW_SECURITY_WPA2_TKIP_PSK) ||
											(join_user_data->network_info.security_type == RTW_SECURITY_WPA2_MIXED_PSK) ||
											(join_user_data->network_info.security_type == RTW_SECURITY_WPA_WPA2_AES_PSK) ||
											(join_user_data->network_info.security_type == RTW_SECURITY_WPA_WPA2_TKIP_PSK) ||
											(join_user_data->network_info.security_type == RTW_SECURITY_WPA_WPA2_MIXED_PSK) ||
											(join_user_data->network_info.security_type == RTW_SECURITY_WPA_AES_PSK) ||
											(join_user_data->network_info.security_type == RTW_SECURITY_WPA_TKIP_PSK) ||
											(join_user_data->network_info.security_type == RTW_SECURITY_WPA_MIXED_PSK)
#ifdef CONFIG_SAE_SUPPORT
											|| (join_user_data->network_info.security_type == RTW_SECURITY_WPA3_AES_PSK)
											|| (join_user_data->network_info.security_type == RTW_SECURITY_WPA2_WPA3_MIXED)
#endif
										   )) {
		rtw_join_status = JOIN_COMPLETE | JOIN_SECURITY_COMPLETE | JOIN_ASSOCIATED | JOIN_AUTHENTICATED | JOIN_LINK_READY | JOIN_CONNECTING;
	}
}
static void wifi_handshake_done_hdl(char *buf, int buf_len, int flags, void *userdata)
{
	/* To avoid gcc warnings */
	(void) buf;
	(void) buf_len;
	(void) flags;
	(void) userdata;

	rtw_join_status = JOIN_COMPLETE | JOIN_SECURITY_COMPLETE | JOIN_ASSOCIATED | JOIN_AUTHENTICATED | JOIN_LINK_READY | JOIN_HANDSHAKE_DONE | JOIN_CONNECTING;
	if (join_user_data != NULL) {
		rtw_up_sema(&join_user_data->join_sema);
	}
}

/*
 * WIFI_EVENT_RX_MGNT event handler for parsing status code of Auth/Assoc/Deauth/Disassoc
 * To use the event, user needs to enable the mgnt indicate as following code.
 * => wifi_set_indicate_mgnt(1);
 * User can register the WIFI_EVENT_RX_MGNT event handler to use, as following code.
 * => wifi_reg_event_handler(WIFI_EVENT_RX_MGNT, rx_mgnt_hdl, NULL);
 */
static void rx_mgnt_hdl(char *buf, int buf_len, int flags, void *userdata)
{
	/* To avoid gcc warnings */
	(void) buf_len;
	(void) flags;
	(void) userdata;

	/* buf detail: mgnt frame*/

	u16 status_code;
	u8 subframe_type;

	subframe_type = (u8)(*(u8 *)buf) & 0xfc;

	switch (subframe_type) {
	case 0x10:
		//Assoc frame: MAC_hdr(24) + Cap_info(2) + status_code(2)
		status_code = (u16)(*(u16 *)(buf + 24 + 2));
		printf("Recv AssocRsp with status_code=%d\n", status_code);
		break;
	case 0xa0:
		//Disassoc frame: MAC_hdr(24) + status_code(2)
		status_code = (u16)(*(u16 *)(buf + 24));
		printf("Recv Disassoc with status_code=%d\n", status_code);
		break;
	case 0xb0:
		//Auth frame: MAC_hdr(24) + Auth_algo(2) + Auth_seq(2) + status_code(2)
		status_code = (u16)(*(u16 *)(buf + 24 + 2 + 2));
		printf("Recv Auth with status_code=%d\n", status_code);
		break;
	case 0xc0:
		//Deauth frame: MAC_hdr(24) + status_code(2)
		status_code = (u16)(*(u16 *)(buf + 24));
		printf("Recv Deauth with status_code=%d\n", status_code);
		break;
	default:
		break;
	}
}

extern void dhcp_stop(struct netif *netif);
#if LWIP_VERSION_MAJOR >= 2 && LWIP_VERSION_MINOR >= 1
#if LWIP_IPV6 && LWIP_IPV6_DHCP6
extern void dhcp6_stop(struct netif *netif);
#endif
#endif

extern u32 rltk_wlan_get_link_err(void);
static void wifi_rssi_report_hdl(char *buf, int buf_len, int flags, void *userdata)
{
	int rssi = flags;
	unsigned char mac_addr[6] = {0};
	memcpy(mac_addr, buf, 6);

	printf("scan report rssi: %d bssid: %02x:%02x:%02x:%02x:%02x:%02x\n", rssi,
		   mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
}

static void wifi_link_err_parse(u16 reason_code)
{
	u32 link_err;
	link_err = rltk_wlan_get_link_err();

	if (link_err != 0U) {
		printf("wifi link err:%08x\r\n", rltk_wlan_get_link_err());
	}

	printf("dissconn reason code: %d\n", reason_code);

	if (link_err & BIT(0)) {
		printf("receive deauth\n");
	} else if (link_err & BIT(1)) {
		printf("receive deassoc\n");
	} else if (link_err & BIT(2)) {
		printf("scan stage, no beacon while connecting\n");
	} else if (link_err & BIT(3)) {
		printf("auth stage, auth timeout\n");
		if (reason_code == 65531) {
			printf("request has been declined\n");
		}
	} else if (link_err & BIT(4)) {
		printf("assoc stage, assoc timeout\n");
	} else if (link_err & (BIT(5) | BIT(6) | BIT(7))) {
		printf("4handshake stage, 4-way waiting timeout\n");
	} else if (link_err & BIT(8)) {
		printf("assoc stage, assoc reject (assoc rsp status > 0)\n");
	} else if (link_err & BIT(9)) {
		printf("auth stage, auth fail (auth rsp status > 0)\n");
	} else if (link_err & BIT(10)) {
		printf("scan stage, scan timeout\n");
	} else if (link_err & BIT(11)) {
		printf("auth stage, WPA3 auth fail, ");
		if (reason_code == 65531) {
			printf("request has been declined\n");
		} else {
			printf("password error\n");
		}
	}

	if (link_err & (BIT(0) | BIT(1))) {
		if (link_err & BIT(20)) {
			printf("handshake done, connected stage, recv deauth/deassoc\n");
		} else if (link_err & BIT(19)) {
			printf("handshake processing, 4handshake stage, recv deauth/deassoc\n");
		} else if (link_err & BIT(18)) {
			printf("assoc successed, 4handshake stage, recv deauth/deassoc\n");
		} else if (link_err & BIT(17)) {
			printf("auth successed, assoc stage, recv deauth/deassoc\n");
		} else if (link_err & BIT(16)) {
			printf("scan done, found ssid, auth stage, recv deauth/deassoc\n");
		} else {
			printf("connected stage, recv deauth/deassoc\n");
		}
	}

	switch (reason_code) {
	case 17:
		printf("auth stage, ap auth full\n");
		break;
	case 65530:
		printf("SA query timeout\n");
		break;
	case 65534:
		printf("connected stage, ap changed\n");
		break;
	case 65535:
		printf("connected stage, loss beacon\n");
		break;
	default:
		break;
	}

	rltk_wlan_set_link_err(0);
}

/*
 * rltk_wlan_get_link_err(): check wifi link error
 *******************************************************
 * err bit:
 * bit  0: recved deauth
 * bit  1: recved disassoc
 * bit  2: no beacon while connecting
 * bit  3: auth timeout
 * bit  4: assoc timeout
 * bit  5: waiting 4-1 timeout
 * bit  6: waiting 4-3 timeout
 * bit  7: waiting 2-1 timeout (WPA needs GTK handshake after 4-way immediately)
 * bit  8: assoc reject (assoc rsp status > 0)
 * bit  9: auth reject (auth rsp status > 0)
 * bit  10: scan timeout
 * bit  11: WPA3 auth fail
 *******************************************************
 * link status bit:
 * bit 16: found ssid
 * bit 17: auth success
 * bit 18: assoc success
 * bit 19: recved 4-1
 * bit 20: recved 4-3
 *******************************************************
 */
static void wifi_disconn_hdl(char *buf, int buf_len, int flags, void *userdata)
{
	/*In WPA3 mode, disconnection may happen if the connection link in the last time does not disconnect properly.*/

	/* To avoid gcc warnings */
	(void) buf_len;
	(void) flags;
	(void) userdata;
#define REASON_4WAY_HNDSHK_TIMEOUT 15
	u16 disconn_reason;
	signed char cnnctfail_reason = 0;
	/* buf detail: mac addr + disconn_reason, buf_len = ETH_ALEN+2*/
	disconn_reason = *(u16 *)(buf + 6);
	cnnctfail_reason = *(signed char *)(buf + 6 + 2);

	wifi_link_err_parse(disconn_reason);

	if (join_user_data != NULL) {
		if (join_user_data->network_info.security_type == RTW_SECURITY_OPEN) {

			if (rtw_join_status & JOIN_NO_NETWORKS) {
				error_flag = RTW_NONE_NETWORK;
			}

			else if (rtw_join_status == JOIN_CONNECTING) {
				if (-1 == cnnctfail_reason) {
					error_flag = RTW_AUTH_FAIL;
				} else if (-4 == cnnctfail_reason) {
					error_flag = RTW_ASSOC_FAIL;
				} else if (-2 == cnnctfail_reason) {
					error_flag = RTW_DEAUTH_DEASSOC;
				} else {
					error_flag = RTW_CONNECT_FAIL;
				}
			}

		} else if (join_user_data->network_info.security_type == RTW_SECURITY_WEP_PSK) {

			if (rtw_join_status & JOIN_NO_NETWORKS) {
				error_flag = RTW_NONE_NETWORK;
			}

			else if (rtw_join_status == JOIN_CONNECTING) {
				if (-1 == cnnctfail_reason) {
					error_flag = RTW_AUTH_FAIL;
				} else if (-4 == cnnctfail_reason) {
					error_flag = RTW_ASSOC_FAIL;
				} else if (-2 == cnnctfail_reason) {
					error_flag = RTW_DEAUTH_DEASSOC;
				} else {
					error_flag = RTW_CONNECT_FAIL;
				}
			}

		} else if (join_user_data->network_info.security_type == RTW_SECURITY_WPA2_AES_PSK ||
				   join_user_data->network_info.security_type == RTW_SECURITY_WPA2_TKIP_PSK ||
				   join_user_data->network_info.security_type == RTW_SECURITY_WPA2_MIXED_PSK ||
				   join_user_data->network_info.security_type == RTW_SECURITY_WPA_WPA2_AES_PSK ||
				   join_user_data->network_info.security_type == RTW_SECURITY_WPA_WPA2_TKIP_PSK ||
				   join_user_data->network_info.security_type == RTW_SECURITY_WPA_WPA2_MIXED_PSK ||
				   join_user_data->network_info.security_type == RTW_SECURITY_WPA_AES_PSK ||
				   join_user_data->network_info.security_type == RTW_SECURITY_WPA_TKIP_PSK ||
				   join_user_data->network_info.security_type == RTW_SECURITY_WPA_MIXED_PSK
#ifdef CONFIG_SAE_SUPPORT
				   || join_user_data->network_info.security_type == RTW_SECURITY_WPA3_AES_PSK
				   || join_user_data->network_info.security_type == RTW_SECURITY_WPA2_WPA3_MIXED
#endif
				  ) {

			if (rtw_join_status & JOIN_NO_NETWORKS) {
				error_flag = RTW_NONE_NETWORK;
			}

			else if (rtw_join_status == JOIN_CONNECTING) {
				if (-1 == cnnctfail_reason) {
					error_flag = RTW_AUTH_FAIL;
				} else if (-4 == cnnctfail_reason) {
					error_flag = RTW_ASSOC_FAIL;
				} else if (-2 == cnnctfail_reason) {
					error_flag = RTW_DEAUTH_DEASSOC;
				} else {
					error_flag = RTW_CONNECT_FAIL;
				}
			} else if (rtw_join_status == (JOIN_COMPLETE | JOIN_SECURITY_COMPLETE | JOIN_ASSOCIATED | JOIN_AUTHENTICATED | JOIN_LINK_READY | JOIN_CONNECTING)) {
				if (disconn_reason == REASON_4WAY_HNDSHK_TIMEOUT) {
					error_flag = RTW_4WAY_HANDSHAKE_TIMEOUT;
				} else {
					error_flag = RTW_WRONG_PASSWORD;
				}
			}
		}
	} else {
		if (error_flag == RTW_NO_ERROR) { //wifi_disconn_hdl will be dispatched one more time after join_user_data = NULL add by frankie
			error_flag = RTW_UNKNOWN;
		}
	}

#if CONFIG_LWIP_LAYER
#if defined(CONFIG_MBED_ENABLED) || defined(CONFIG_PLATFOMR_CUSTOMER_RTOS)
	//TODO
#else
	dhcp_stop(&xnetif[0]);
#if LWIP_VERSION_MAJOR >= 2 && LWIP_VERSION_MINOR >= 1
#if LWIP_IPV6 && LWIP_IPV6_DHCP6
	dhcp6_stop(&xnetif[0]);
#endif
#endif
#if LWIP_ARP
	if (xnetif[0].flags & NETIF_FLAG_ETHARP) {
		etharp_cleanup_netif(&xnetif[0]);
	}
#endif /* LWIP_ARP */
#endif
#endif

	if (join_user_data != NULL) {
		rtw_up_sema(&join_user_data->join_sema);
	}
	//RTW_API_INFO("\r\nWiFi Disconnect. Error flag is %d.\n", error_flag);

// Need to use sema to make sure wifi_disconn_hdl invoked before setting join_user_data when connecting to another AP
#if CONFIG_WIFI_IND_USE_THREAD
	if (disconnect_sema != NULL) {
		rtw_up_sema(&disconnect_sema);
	}
#endif
}

#if CONFIG_EXAMPLE_WLAN_FAST_CONNECT || CONFIG_JD_SMART
#define WLAN0_NAME	      "wlan0"

#if defined(CONFIG_FAST_DHCP) && CONFIG_FAST_DHCP
u8 is_the_same_ap = 0;

void check_is_the_same_ap()
{
	flash_t		flash;
	struct wlan_fast_reconnect data;
	u8 *ifname[1] = {WLAN0_NAME};
	rtw_wifi_setting_t setting;

	if (wifi_get_setting((const char *)ifname[0], &setting) || setting.mode == RTW_MODE_AP) {
		RTW_API_INFO("\r\n %s():wifi_get_setting fail or ap mode", __FUNCTION__);
		return;
	}

	device_mutex_lock(RT_DEV_LOCK_FLASH);
	flash_stream_read(&flash, FAST_RECONNECT_DATA, sizeof(struct wlan_fast_reconnect), (uint8_t *)&data);
	device_mutex_unlock(RT_DEV_LOCK_FLASH);

	if (strncmp((char const *)data.psk_essid, (char const *)setting.ssid, strlen((char const *)setting.ssid)) == 0) {
		is_the_same_ap = 1;
	} else {
		is_the_same_ap = 0;
	}
}
#endif

void restore_wifi_info_to_flash(uint32_t offer_ip, uint32_t server_ip)
{
	u32 channel = 0;
	u8 index = 0;
	u8 *ifname[1] = {(u8 *)WLAN0_NAME};
	rtw_wifi_setting_t setting;
	struct wlan_fast_reconnect wifi_data_to_flash;
	//struct security_priv *psecuritypriv = &padapter->securitypriv;
	//WLAN_BSSID_EX  *pcur_bss = pmlmepriv->cur_network.network;

	rtw_memset(&wifi_data_to_flash, 0, sizeof(struct wlan_fast_reconnect));

	if (p_write_reconnect_ptr) {
		if (wifi_get_setting((const char *)ifname[0], &setting) || setting.mode == RTW_MODE_AP) {
			RTW_API_INFO("\r\n %s():wifi_get_setting fail or ap mode", __func__);
			return;
		}
		channel = setting.channel;
#if defined(CONFIG_FAST_DHCP) && CONFIG_FAST_DHCP
		rtw_memset(&wifi_data_to_flash, 0, sizeof(struct wlan_fast_reconnect));
#endif

		rtw_memset(psk_essid[index], 0, sizeof(psk_essid[index]));
		strncpy((char *)psk_essid[index], (char const *)setting.ssid, strlen((char const *)setting.ssid));
		switch (setting.security_type) {
		case RTW_SECURITY_OPEN:
			rtw_memset(psk_passphrase[index], 0, sizeof(psk_passphrase[index]));
			rtw_memset(wpa_global_PSK[index], 0, sizeof(wpa_global_PSK[index]));
			wifi_data_to_flash.security_type = RTW_SECURITY_OPEN;
			break;
		case RTW_SECURITY_WEP_PSK:
			channel |= (setting.key_idx) << 28;
			rtw_memset(psk_passphrase[index], 0, sizeof(psk_passphrase[index]));
			rtw_memset(wpa_global_PSK[index], 0, sizeof(wpa_global_PSK[index]));
			rtw_memcpy(psk_passphrase[index], setting.password, sizeof(psk_passphrase[index]));
			wifi_data_to_flash.security_type = RTW_SECURITY_WEP_PSK;
			break;
		case RTW_SECURITY_WPA_AES_PSK:
		case RTW_SECURITY_WPA_TKIP_PSK:
		case RTW_SECURITY_WPA_MIXED_PSK:
		case RTW_SECURITY_WPA2_AES_PSK:
		case RTW_SECURITY_WPA2_TKIP_PSK:
		case RTW_SECURITY_WPA2_MIXED_PSK:
		case RTW_SECURITY_WPA_WPA2_AES_PSK:
		case RTW_SECURITY_WPA_WPA2_TKIP_PSK:
		case RTW_SECURITY_WPA_WPA2_MIXED_PSK:
#ifdef CONFIG_SAE_SUPPORT
		case RTW_SECURITY_WPA3_AES_PSK:
		case RTW_SECURITY_WPA2_WPA3_MIXED:
#endif
			wifi_data_to_flash.security_type = setting.security_type;
			break;
		default:
			break;
		}

		memcpy(wifi_data_to_flash.psk_essid, psk_essid[index], sizeof(wifi_data_to_flash.psk_essid));
		if (strlen((char const *)psk_passphrase64) == IW_WPA2_PASSPHRASE_MAX_SIZE) {
			memcpy(wifi_data_to_flash.psk_passphrase, psk_passphrase64, sizeof(wifi_data_to_flash.psk_passphrase));
		} else {
			memcpy(wifi_data_to_flash.psk_passphrase, psk_passphrase[index], sizeof(wifi_data_to_flash.psk_passphrase));
		}

		//For avoiding 13Bytes password of WEP ascii replacing WPA that will cause four-way handshaking failed because of not generating global_psk.
		if (setting.security_type == RTW_SECURITY_WEP_PSK) {
			rtw_memset(psk_passphrase[index], 0, sizeof(psk_passphrase[index]));
		}

		memcpy(wifi_data_to_flash.wpa_global_PSK, wpa_global_PSK[index], sizeof(wifi_data_to_flash.wpa_global_PSK));
		memcpy(&(wifi_data_to_flash.channel), &channel, 4);

#if CONFIG_FAST_DHCP
		wifi_data_to_flash.offer_ip = offer_ip;
		wifi_data_to_flash.server_ip = server_ip;
#endif
		//call callback function in user program
		p_write_reconnect_ptr((u8 *)&wifi_data_to_flash, sizeof(struct wlan_fast_reconnect));

	}
}

#endif

//----------------------------------------------------------------------------//
int wifi_connect(
	char 				*ssid,
	rtw_security_t	security_type,
	char 				*password,
	int 				ssid_len,
	int 				password_len,
	int 				key_id,
	void 				*semaphore)
{
	_sema join_semaphore = {0};
	unsigned int join_timeout;
	rtw_result_t result = RTW_SUCCESS;
	u8 wep_hex = 0;
	u8 wep_pwd[14] = {0};

	if (rtw_join_status & JOIN_CONNECTING) {
		if (wifi_disconnect() < 0) {
			RTW_API_INFO("\nwifi_disconnect Operation failed!");
			return RTW_ERROR;
		}
		while (rtw_join_status & JOIN_CONNECTING) {
			rtw_msleep_os(1);
		}
	}

	if (rtw_join_status & JOIN_SIMPLE_CONFIG || rtw_join_status & JOIN_AIRKISS) {
		return RTW_BUSY;
	}

	error_flag = RTW_UNKNOWN ;//clear for last connect status
	rtw_memset(ap_bssid, 0, ETH_ALEN);

	/* check password length for WPA3 */
#ifdef CONFIG_SAE_SUPPORT
	if (((password_len > RTW_WPA3_MAX_PSK_LEN) ||
		 (password_len < RTW_MIN_PSK_LEN)) &&
		((security_type == RTW_SECURITY_WPA3_AES_PSK) ||
		 (security_type == RTW_SECURITY_WPA2_WPA3_MIXED))) {
		error_flag = RTW_WRONG_PASSWORD;
		return RTW_INVALID_KEY;
	}
#endif

	/* check password length for WPA/WPA2 */
	if (((password_len > RTW_WPA2_MAX_PSK_LEN) ||
		 (password_len < RTW_MIN_PSK_LEN)) &&
		((security_type == RTW_SECURITY_WPA_TKIP_PSK) ||
		 (security_type == RTW_SECURITY_WPA_AES_PSK) ||
		 (security_type == RTW_SECURITY_WPA_MIXED_PSK) ||
		 (security_type == RTW_SECURITY_WPA2_TKIP_PSK) ||
		 (security_type == RTW_SECURITY_WPA2_AES_PSK) ||
		 (security_type == RTW_SECURITY_WPA2_MIXED_PSK) ||
		 (security_type == RTW_SECURITY_WPA_WPA2_TKIP_PSK) ||
		 (security_type == RTW_SECURITY_WPA_WPA2_AES_PSK) ||
		 (security_type == RTW_SECURITY_WPA_WPA2_MIXED_PSK))) {
		error_flag = RTW_WRONG_PASSWORD;
		return RTW_INVALID_KEY;
	}

	/* check password length and convert hex format to ASCII format for WEP */
	if ((security_type == RTW_SECURITY_WEP_PSK) ||
		(security_type == RTW_SECURITY_WEP_SHARED)) {
		if ((password_len != 5) && (password_len != 13) &&
			(password_len != 10) && (password_len != 26)) {
			error_flag = RTW_WRONG_PASSWORD;
			return RTW_INVALID_KEY;
		} else {
			if (password_len == 10) {
				unsigned int p[5] = {0};
				u8 i = 0;
				sscanf((const char *)password, "%02x%02x%02x%02x%02x", &p[0], &p[1], &p[2], &p[3], &p[4]);
				for (i = 0; i < 5; i++) {
					wep_pwd[i] = (u8)p[i];
				}
				wep_pwd[5] = '\0';
				password_len = 5;
				wep_hex = 1;
			} else if (password_len == 26) {
				unsigned int p[13] = {0};
				u8 i = 0;
				sscanf((const char *)password, "%02x%02x%02x%02x%02x%02x%02x"\
					   "%02x%02x%02x%02x%02x%02x", &p[0], &p[1], &p[2], &p[3], &p[4], \
					   &p[5], &p[6], &p[7], &p[8], &p[9], &p[10], &p[11], &p[12]);
				for (i = 0; i < 13; i++) {
					wep_pwd[i] = (u8)p[i];
				}
				wep_pwd[13] = '\0';
				password_len = 13;
				wep_hex = 1;
			}
		}
	}

	/* check SSID length */
	if ((ssid_len < RTW_MIN_SSID_LEN) || (ssid_len > RTW_MAX_SSID_LEN)) {
		error_flag = RTW_NONE_NETWORK;
		return RTW_BADSSIDLEN;
	}

	/* copy the connection setting about SSID, BSSID, security type and password, etc. */
	internal_join_result_t *join_result = (internal_join_result_t *)rtw_zmalloc(sizeof(internal_join_result_t));
	if (!join_result) {
		return RTW_NOMEM;
	}

	join_result->network_info.ssid.len = ssid_len;
	rtw_memcpy(join_result->network_info.ssid.val, ssid, join_result->network_info.ssid.len);

	join_result->network_info.password_len = password_len;
	if (password_len) {
		/* add \0 to the end */
		join_result->network_info.password = rtw_zmalloc(password_len);
		if (!join_result->network_info.password) {
			result = (rtw_result_t) RTW_NOMEM;
			goto error;
		}
		if (0 == wep_hex) {
			rtw_memcpy(join_result->network_info.password, password, password_len);
		} else {
			rtw_memcpy(join_result->network_info.password, wep_pwd, password_len);
		}

	}

	join_result->network_info.security_type = security_type;
	join_result->network_info.key_id = key_id;

	if (semaphore == NULL) {
		rtw_init_sema(&join_result->join_sema, 0);
		if (!join_result->join_sema) {
			result = (rtw_result_t) RTW_NORESOURCE;
			goto error;
		}
		join_semaphore = join_result->join_sema;
	} else {
		join_result->join_sema = semaphore;
	}

	/* register event handler */
	wifi_reg_event_handler(WIFI_EVENT_NO_NETWORK, wifi_no_network_hdl, NULL);
	wifi_reg_event_handler(WIFI_EVENT_CONNECT, wifi_connected_hdl, NULL);
	wifi_reg_event_handler(WIFI_EVENT_DISCONNECT, wifi_disconn_hdl, NULL);
	wifi_reg_event_handler(WIFI_EVENT_FOURWAY_HANDSHAKE_DONE, wifi_handshake_done_hdl, NULL);
	wifi_reg_event_handler(WIFI_EVENT_TARGET_SSID_RSSI, wifi_rssi_report_hdl, NULL);

// if is connected to ap, would trigger disconn_hdl but need to make sure it is invoked before setting join_user_data
#if CONFIG_WIFI_IND_USE_THREAD
	if (wifi_is_connected_to_ap() == RTW_SUCCESS) {
		rtw_init_sema(&disconnect_sema, 0);
	}
#endif

	/* connect with ssid */
	rtw_join_status = JOIN_CONNECTING;
	result = (rtw_result_t)wifi_connect_local(&join_result->network_info);
	if (result != 0) {
		goto error;
	}

#if CONFIG_WIFI_IND_USE_THREAD
	if (disconnect_sema != NULL) {
		rtw_down_timeout_sema(&disconnect_sema, 50);
		rtw_free_sema(&disconnect_sema);
	}
#endif

	join_user_data = join_result;

	/* wait connect finished for synchronous connection */
	if (semaphore == NULL) {
#ifdef CONFIG_ENABLE_EAP
		// for eap connection, timeout should be longer (default value in wpa_supplicant: 60s)
		if (get_eap_phase()) {
			join_timeout = 60000;
		} else
#endif
		{
			join_timeout = RTW_JOIN_TIMEOUT;
		}

		if (rtw_down_timeout_sema(&join_result->join_sema, join_timeout) == RTW_FALSE) {
			RTW_API_INFO("RTW API: Join bss timeout\r\n");
			result = RTW_TIMEOUT;
			goto error;
		} else {
			if (wifi_is_connected_to_ap() != RTW_SUCCESS) {
				result = RTW_ERROR;
				goto error;
			}
		}
	}

	result = RTW_SUCCESS;
#if CONFIG_LWIP_LAYER
#if defined(CONFIG_MBED_ENABLED) || defined(CONFIG_PLATFOMR_CUSTOMER_RTOS)
	//TODO
#else
	netif_set_link_up(&xnetif[0]);
#endif
#endif

#if CONFIG_EXAMPLE_WLAN_FAST_CONNECT || CONFIG_JD_SMART
#if CONFIG_FAST_DHCP
	check_is_the_same_ap();
#else
	restore_wifi_info_to_flash(0, 0);
#endif
#endif

error:
	if (semaphore == NULL) {
		rtw_free_sema(&join_semaphore);
	}
	join_user_data = NULL;
	if (join_result->network_info.password) {
		rtw_free(join_result->network_info.password);
	}
	rtw_free((u8 *)join_result);
	wifi_unreg_event_handler(WIFI_EVENT_CONNECT, wifi_connected_hdl);
	wifi_unreg_event_handler(WIFI_EVENT_NO_NETWORK, wifi_no_network_hdl);
	wifi_unreg_event_handler(WIFI_EVENT_FOURWAY_HANDSHAKE_DONE, wifi_handshake_done_hdl);
	wifi_unreg_event_handler(WIFI_EVENT_TARGET_SSID_RSSI, wifi_rssi_report_hdl);
	rtw_join_status &= ~JOIN_CONNECTING;
	return result;
}

int wifi_connect_bssid(
	unsigned char 		bssid[ETH_ALEN],
	char 				*ssid,
	rtw_security_t	security_type,
	char 				*password,
	int 				bssid_len,
	int 				ssid_len,
	int 				password_len,
	int 				key_id,
	void 				*semaphore)
{
	_sema join_semaphore = {0};
	unsigned int join_timeout;
	rtw_result_t result = RTW_SUCCESS;
	u8 wep_hex = 0;
	u8 wep_pwd[14] = {0};

	if (rtw_join_status & JOIN_CONNECTING) {
		if (wifi_disconnect() < 0) {
			RTW_API_INFO("\nwifi_disconnect Operation failed!");
			return RTW_ERROR;
		}
		while (rtw_join_status & JOIN_CONNECTING) {
			rtw_msleep_os(1);
		}
	}

	if (rtw_join_status & JOIN_SIMPLE_CONFIG || rtw_join_status & JOIN_AIRKISS) {
		return RTW_BUSY;
	}

	error_flag = RTW_UNKNOWN;//clear for last connect status
	rtw_memset(ap_bssid, 0, ETH_ALEN);

	/* check password length for WPA3 */
#ifdef CONFIG_SAE_SUPPORT
	if (((password_len > RTW_WPA3_MAX_PSK_LEN) ||
		 (password_len < RTW_MIN_PSK_LEN)) &&
		((security_type == RTW_SECURITY_WPA3_AES_PSK) ||
		 (security_type == RTW_SECURITY_WPA2_WPA3_MIXED))) {
		error_flag = RTW_WRONG_PASSWORD;
		return RTW_INVALID_KEY;
	}
#endif

	/* check password length for WPA/WPA2 */
	if (((password_len > RTW_WPA2_MAX_PSK_LEN) ||
		 (password_len < RTW_MIN_PSK_LEN)) &&
		((security_type == RTW_SECURITY_WPA_TKIP_PSK) ||
		 (security_type == RTW_SECURITY_WPA_AES_PSK) ||
		 (security_type == RTW_SECURITY_WPA_MIXED_PSK) ||
		 (security_type == RTW_SECURITY_WPA2_TKIP_PSK) ||
		 (security_type == RTW_SECURITY_WPA2_AES_PSK) ||
		 (security_type == RTW_SECURITY_WPA2_MIXED_PSK) ||
		 (security_type == RTW_SECURITY_WPA_WPA2_TKIP_PSK) ||
		 (security_type == RTW_SECURITY_WPA_WPA2_AES_PSK) ||
		 (security_type == RTW_SECURITY_WPA_WPA2_MIXED_PSK))) {
		error_flag = RTW_WRONG_PASSWORD;
		return RTW_INVALID_KEY;
	}

	/* check password length and convert hex format to ASCII format for WEP */
	if ((security_type == RTW_SECURITY_WEP_PSK) ||
		(security_type == RTW_SECURITY_WEP_SHARED)) {
		if ((password_len != 5) && (password_len != 13) &&
			(password_len != 10) && (password_len != 26)) {
			error_flag = RTW_WRONG_PASSWORD;
			return RTW_INVALID_KEY;
		} else {
			if (password_len == 10) {
				unsigned int p[5] = {0};
				u8 i = 0;
				sscanf((const char *)password, "%02x%02x%02x%02x%02x", &p[0], &p[1], &p[2], &p[3], &p[4]);
				for (i = 0; i < 5; i++) {
					wep_pwd[i] = (u8)p[i];
				}
				wep_pwd[5] = '\0';
				password_len = 5;
				wep_hex = 1;
			} else if (password_len == 26) {
				unsigned int p[13] = {0};
				u8 i = 0;
				sscanf((const char *)password, "%02x%02x%02x%02x%02x%02x%02x"\
					   "%02x%02x%02x%02x%02x%02x", &p[0], &p[1], &p[2], &p[3], &p[4], \
					   &p[5], &p[6], &p[7], &p[8], &p[9], &p[10], &p[11], &p[12]);
				for (i = 0; i < 13; i++) {
					wep_pwd[i] = (u8)p[i];
				}
				wep_pwd[13] = '\0';
				password_len = 13;
				wep_hex = 1;
			}
		}
	}

	/* check SSID length */
	if (ssid && ((ssid_len < RTW_MIN_SSID_LEN) || (ssid_len > RTW_MAX_SSID_LEN))) {
		error_flag = RTW_NONE_NETWORK;
		return RTW_BADSSIDLEN;
	}

	/* check BSSID length */
	if (bssid_len != ETH_ALEN) {
		error_flag = RTW_NONE_NETWORK;
		return RTW_BADSSIDLEN;
	}

	/* copy the connection setting about SSID, BSSID, security type and password, etc. */
	internal_join_result_t *join_result = (internal_join_result_t *)rtw_zmalloc(sizeof(internal_join_result_t));
	if (!join_result) {
		return RTW_NOMEM;
	}

	if (ssid_len && ssid) {
		join_result->network_info.ssid.len = ssid_len;
		rtw_memcpy(join_result->network_info.ssid.val, ssid, join_result->network_info.ssid.len);
	}
	rtw_memcpy(join_result->network_info.bssid.octet, bssid, bssid_len);

	join_result->network_info.password_len = password_len;
	if (password_len) {
		/* add \0 to the end */
		join_result->network_info.password = rtw_zmalloc(password_len);
		if (!join_result->network_info.password) {
			result = (rtw_result_t) RTW_NOMEM;
			goto error;
		}
		if (0 == wep_hex) {
			rtw_memcpy(join_result->network_info.password, password, password_len);
		} else {
			rtw_memcpy(join_result->network_info.password, wep_pwd, password_len);
		}
	}

	join_result->network_info.security_type = security_type;
	join_result->network_info.key_id = key_id;

	if (semaphore == NULL) {
		rtw_init_sema(&join_result->join_sema, 0);
		if (!join_result->join_sema) {
			result = (rtw_result_t) RTW_NORESOURCE;
			goto error;
		}
		join_semaphore = join_result->join_sema;
	} else {
		join_result->join_sema = semaphore;
	}

	/* register event handler*/
	wifi_reg_event_handler(WIFI_EVENT_NO_NETWORK, wifi_no_network_hdl, NULL);
	wifi_reg_event_handler(WIFI_EVENT_CONNECT, wifi_connected_hdl, NULL);
	wifi_reg_event_handler(WIFI_EVENT_DISCONNECT, wifi_disconn_hdl, NULL);
	wifi_reg_event_handler(WIFI_EVENT_FOURWAY_HANDSHAKE_DONE, wifi_handshake_done_hdl, NULL);
	wifi_reg_event_handler(WIFI_EVENT_TARGET_SSID_RSSI, wifi_rssi_report_hdl, NULL);

// if is connected to ap, would trigger disconn_hdl but need to make sure it is invoked before setting join_user_data
#if CONFIG_WIFI_IND_USE_THREAD
	if (wifi_is_connected_to_ap() == RTW_SUCCESS) {
		rtw_init_sema(&disconnect_sema, 0);
	}
#endif

	/* connect with bssid */
	rtw_join_status = JOIN_CONNECTING;
	result = (rtw_result_t)wifi_connect_bssid_local(&join_result->network_info);
	if (result != 0) {
		goto error;
	}

#if CONFIG_WIFI_IND_USE_THREAD
	if (disconnect_sema != NULL) {
		rtw_down_timeout_sema(&disconnect_sema, 50);
		rtw_free_sema(&disconnect_sema);
	}
#endif

	join_user_data = join_result;

	/* wait connect finished for synchronous connection */
	if (semaphore == NULL) {
#ifdef CONFIG_ENABLE_EAP
		// for eap connection, timeout should be longer (default value in wpa_supplicant: 60s)
		if (get_eap_phase()) {
			join_timeout = 60000;
		} else
#endif
		{
			join_timeout = RTW_JOIN_TIMEOUT;
		}

		if (rtw_down_timeout_sema(&join_result->join_sema, join_timeout) == RTW_FALSE) {
			RTW_API_INFO("RTW API: Join bss timeout\r\n");
			result = RTW_TIMEOUT;
			goto error;
		} else {
			if (wifi_is_connected_to_ap() != RTW_SUCCESS) {
				result = RTW_ERROR;
				goto error;
			}
		}
	}

	result = RTW_SUCCESS;

#if CONFIG_LWIP_LAYER
#if defined(CONFIG_MBED_ENABLED) || defined(CONFIG_PLATFOMR_CUSTOMER_RTOS)
	//TODO
#else
	netif_set_link_up(&xnetif[0]);
#endif
#endif

#if CONFIG_EXAMPLE_WLAN_FAST_CONNECT || CONFIG_JD_SMART
#if CONFIG_FAST_DHCP
	check_is_the_same_ap();
#else
	restore_wifi_info_to_flash(0, 0);
#endif

#endif

error:
	if (semaphore == NULL) {
		rtw_free_sema(&join_semaphore);
	}
	join_user_data = NULL;
	if (join_result->network_info.password) {
		rtw_free(join_result->network_info.password);
	}
	rtw_free((u8 *)join_result);
	wifi_unreg_event_handler(WIFI_EVENT_CONNECT, wifi_connected_hdl);
	wifi_unreg_event_handler(WIFI_EVENT_NO_NETWORK, wifi_no_network_hdl);
	wifi_unreg_event_handler(WIFI_EVENT_FOURWAY_HANDSHAKE_DONE, wifi_handshake_done_hdl);
	wifi_unreg_event_handler(WIFI_EVENT_TARGET_SSID_RSSI, wifi_rssi_report_hdl);
	rtw_join_status &= ~JOIN_CONNECTING;
	return result;
}

int wifi_disconnect(void)
{
	int ret = 0;

	//set MAC address last byte to 1 since driver will filter the mac with all 0x00 or 0xff
	//add extra 2 zero byte for check of #@ in wext_set_bssid()
	const __u8 null_bssid[ETH_ALEN + 6] = {0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0};

	if (wext_set_bssid(WLAN0_NAME, null_bssid) < 0) {
		RTW_API_INFO("\n\rWEXT: Failed to set bogus BSSID to disconnect");
		ret = -1;
	}
	return ret;
}

//----------------------------------------------------------------------------//
int wifi_is_connected_to_ap(void)
{
	return rltk_wlan_is_connected_to_ap();
}

//----------------------------------------------------------------------------//
void wifi_set_partial_scan_retry_times(unsigned char times)
{
	rltk_wlan_set_partial_scan_retry_times(times);
}

//----------------------------------------------------------------------------//
int wifi_is_up(rtw_interface_t interface)
{
	switch (interface) {
	case RTW_AP_INTERFACE:
		switch (wifi_mode) {
		case RTW_MODE_STA_AP:
			return (rltk_wlan_running(WLAN1_IDX) && _wifi_is_on);
		case RTW_MODE_STA:
			return 0;
		default:
			return (rltk_wlan_running(WLAN0_IDX) && _wifi_is_on);
		}
	case RTW_STA_INTERFACE:
		switch (wifi_mode) {
		case RTW_MODE_AP:
			return 0;
		default:
			return (rltk_wlan_running(WLAN0_IDX) && _wifi_is_on);
		}
	default:
		return 0;
	}
}

int wifi_is_ready_to_transceive(rtw_interface_t interface)
{
	switch (interface) {
	case RTW_AP_INTERFACE:
		return (wifi_is_up(interface) == RTW_TRUE) ? RTW_SUCCESS : RTW_ERROR;

	case RTW_STA_INTERFACE:
		switch (error_flag) {
		case RTW_NO_ERROR:
			return RTW_SUCCESS;

		default:
			return RTW_ERROR;
		}
	default:
		return RTW_ERROR;
	}
}

/**
Example:
u8 mac[ETH_ALEN] = {0x00, 0xe0, 0x4c, 0x87, 0x12, 0x34};
wifi_change_mac_address_from_ram(mac);
This method is to modify the mac and don't write to efuse.
**/
extern int rltk_change_mac_address_from_ram(int idx, u8 *mac);
int wifi_change_mac_address_from_ram(int idx, u8 *mac)
{
	int ret = 0;
	int ret_ps = 0;
	const char *ifname = WLAN0_NAME;
	if ((0 != idx) && (1 != idx)) {
		RTW_API_INFO("\n\rInvalid interface selected");
		return RTW_ERROR;
	}
	if (1 == idx) {
		ifname = WLAN1_NAME;
	}
	ret_ps = wext_disable_powersave(ifname);
	if (RTW_SUCCESS != ret_ps) {
		RTW_API_INFO("\n\rFailed to disable powersave");
	}
	ret = rltk_change_mac_address_from_ram(idx, mac);
	ret_ps = wext_resume_powersave(ifname);
	if (RTW_SUCCESS != ret_ps) {
		RTW_API_INFO("\n\rFailed to resume powersave");
	}
	return ret;
}

//----------------------------------------------------------------------------//
int wifi_set_mac_address(char *mac)
{
	char buf[13 + 17 + 1];
	rtw_memset(buf, 0, sizeof(buf));
	snprintf(buf, 13 + 17, "write_mac %s", mac);
	return wext_private_command(WLAN0_NAME, buf, 0);
}

int wifi_get_mac_address(char *mac)
{
	int ret = 0;
	char buf[32] = {0};
	rtw_memset(buf, 0, sizeof(buf));
	rtw_memcpy(buf, "read_mac", 8);
	ret = wext_private_command_with_retval(WLAN0_NAME, buf, buf, 32);
	strcpy(mac, buf);
	return ret;
}

//----------------------------------------------------------------------------//
int wifi_enable_powersave(void)
{
	return wext_enable_powersave(WLAN0_NAME, 1, 1);
}

/* wext_resume_powersave() will result in calling sscanf,
 sscanf have stack size usage different on IAR and GCC.
 use rltk_wlan_resume_powersave() to avoid calling of sscanf */
extern int rltk_wlan_resume_powersave(void);
extern int rltk_wlan_disable_powersave(void);
int wifi_resume_powersave(void)
{
	return rltk_wlan_resume_powersave();
}

int wifi_disable_powersave(void)
{
	return rltk_wlan_disable_powersave();
}

void wifi_btcoex_set_bt_on(void)
{
	rltk_wlan_btcoex_set_bt_state(1);
}

void wifi_btcoex_set_bt_off(void)
{
	rltk_wlan_btcoex_set_bt_state(0);
}


#if 0 //Not ready
//----------------------------------------------------------------------------//
int wifi_get_txpower(int *poweridx)
{
	int ret = 0;
	char buf[11];

	rtw_memset(buf, 0, sizeof(buf));
	rtw_memcpy(buf, "txpower", 11);
	ret = wext_private_command_with_retval(WLAN0_NAME, buf, buf, 11);
	sscanf(buf, "%d", poweridx);

	return ret;
}

int wifi_set_txpower(int poweridx)
{
	int ret = 0;
	char buf[24];

	rtw_memset(buf, 0, sizeof(buf));
	snprintf(buf, 24, "txpower patha=%d", poweridx);
	ret = wext_private_command(WLAN0_NAME, buf, 0);

	return ret;
}
#endif

//----------------------------------------------------------------------------//
int wifi_get_associated_client_list(void *client_list_buffer, uint16_t buffer_length)
{
	/* To avoid gcc warnings */
	(void) buffer_length;

	const char *ifname = WLAN0_NAME;
	int ret = 0;
	char buf[25];

	if (wifi_mode == RTW_MODE_STA_AP) {
		ifname = WLAN1_NAME;
	}

	rtw_memset(buf, 0, sizeof(buf));
	snprintf(buf, 25, "get_client_list %x", (unsigned int)client_list_buffer);
	ret = wext_private_command(ifname, buf, 0);

	return ret;
}

//----------------------------------------------------------------------------//
int wifi_get_ap_bssid(unsigned char *bssid)
{
	if (RTW_SUCCESS == wifi_is_ready_to_transceive(RTW_STA_INTERFACE)) {
		rtw_memcpy(bssid, ap_bssid, ETH_ALEN);
		return RTW_SUCCESS;
	}
	return RTW_ERROR;
}

//----------------------------------------------------------------------------//
int wifi_get_ap_info(rtw_bss_info_t *ap_info, rtw_security_t *security)
{
	const char *ifname = WLAN0_NAME;
	int ret = 0;
	char buf[24];
	int tmp = 0;

	if (wifi_mode == RTW_MODE_STA_AP) {
		ifname = WLAN1_NAME;
	}

	rtw_memset(buf, 0, sizeof(buf));
	snprintf(buf, 24, "get_ap_info %x", (unsigned int)ap_info);
	ret = wext_private_command(ifname, buf, 0);

	snprintf(buf, 24, "get_security");
	ret = wext_private_command_with_retval(ifname, buf, buf, 24);
	sscanf(buf, "%d", &tmp);
	*security = (rtw_security_t)tmp;

	return ret;
}

int wifi_get_drv_ability(uint32_t *ability)
{
	return wext_get_drv_ability(WLAN0_NAME, (__u32 *)ability);
}

//----------------------------------------------------------------------------//
int wifi_set_country(rtw_country_code_t country_code)
{
	int ret;

	ret = wext_set_country(WLAN0_NAME, country_code);

	if ((ret == 0) && (wifi_mode == RTW_MODE_STA_AP)) {
		ret = wext_set_country(WLAN1_NAME, country_code);
	}

	return ret;
}

int wifi_get_country(rtw_country_code_t *country_code)
{
	int ret;

	ret = rltk_wlan_get_country_code(WLAN0_NAME, country_code);

	return ret;
}

//----------------------------------------------------------------------------//
int wifi_change_channel_plan(uint8_t channel_plan)
{
	int ret;

	ret = rltk_wlan_change_channel_plan(channel_plan);

	return ret;
}

//----------------------------------------------------------------------------//
int wifi_set_channel_plan(uint8_t channel_plan)
{
	const char *ifname = WLAN0_NAME;
	int ret = 0;
	char buf[24];

	rtw_memset(buf, 0, sizeof(buf));
	snprintf(buf, 24, "set_ch_plan %x", channel_plan);
	ret = wext_private_command(ifname, buf, 0);
	return ret;
}

int wifi_get_channel_plan(uint8_t *channel_plan)
{
	int ret = 0;
	char buf[24] = {0};
	char *ptmp;

	rtw_memset(buf, 0, sizeof(buf));
	rtw_memcpy(buf, "get_ch_plan", 11);
	ret = wext_private_command_with_retval(WLAN0_NAME, buf, buf, 24);
	*channel_plan = strtoul(buf, &ptmp, 16);
	return ret;
}

//----------------------------------------------------------------------------//
extern int rltk_wlan_get_sta_max_data_rate(u8 *inidata_rate);
int wifi_get_sta_max_data_rate(OUT u8 *inidata_rate)
{
	return rltk_wlan_get_sta_max_data_rate(inidata_rate);
}

//----------------------------------------------------------------------------//
int wifi_get_rssi(int *pRSSI)
{
	return wext_get_rssi(WLAN0_NAME, pRSSI);
}
//----------------------------------------------------------------------------//
extern int wext_get_bcn_rssi(const char *ifname, int *rssi);
int wifi_get_bcn_rssi(int *pRSSI)
{
	return wext_get_bcn_rssi(WLAN0_NAME, pRSSI);
}
//----------------------------------------------------------------------------//
int wifi_get_snr(int *pSNR)
{
	return wext_get_snr(WLAN0_NAME, pSNR);
}

//----------------------------------------------------------------------------//
int wifi_set_channel(int channel)
{
	return wext_set_channel(WLAN0_NAME, channel);
}

int wifi_get_channel(int *channel)
{
	return wext_get_channel(WLAN0_NAME, (u8 *)channel);
}

//----------------------------------------------------------------------------//
int wifi_register_multicast_address(rtw_mac_t *mac)
{
	return wext_register_multicast_address(WLAN0_NAME, mac);
}

int wifi_unregister_multicast_address(rtw_mac_t *mac)
{
	return wext_unregister_multicast_address(WLAN0_NAME, mac);
}

//----------------------------------------------------------------------------//
_WEAK void wifi_set_mib(void)
{
	// adaptivity
	wext_set_adaptivity(RTW_ADAPTIVITY_DISABLE);
	//trp tis
	wext_set_trp_tis(RTW_TRP_TIS_DISABLE);
#ifdef CONFIG_SAE_SUPPORT
	// set to 'ENABLE' when using WPA3
	wext_set_support_wpa3(ENABLE);
#endif
}

//----------------------------------------------------------------------------//
_WEAK void wifi_set_country_code(void)
{
	//wifi_set_country(RTW_COUNTRY_US);
}

//----------------------------------------------------------------------------//
int wifi_rf_on(void)
{
	int ret;
	ret = rltk_wlan_rf_on();
	return ret;
}

//----------------------------------------------------------------------------//
int wifi_rf_off(void)
{
	int ret;
	ret = rltk_wlan_rf_off();
	return ret;
}

//----------------------------------------------------------------------------//
int wifi_on(rtw_mode_t mode)
{
	int ret = 1;
	int timeout = 20;
	int idx;
	int devnum = 1;
	static int event_init = 0;

	device_mutex_lock(RT_DEV_LOCK_WLAN);
	if (rltk_wlan_running(WLAN0_IDX)) {
		RTW_API_INFO("\n\rWIFI is already running");
		device_mutex_unlock(RT_DEV_LOCK_WLAN);
		return 1;
	}

	if (event_init == 0) {
		init_event_callback_list();
		event_init = 1;
	}

	wifi_mode = mode;

	if (mode == RTW_MODE_STA_AP) {
		devnum = 2;
	}

	// set wifi mib
	wifi_set_mib();
	RTW_API_INFO("\n\rInitializing WIFI ...");
	for (idx = 0; idx < devnum; idx++) {
		ret = rltk_wlan_init(idx, mode);
		if (ret < 0) {
			wifi_mode = RTW_MODE_NONE;
			device_mutex_unlock(RT_DEV_LOCK_WLAN);
			return ret;
		}
	}
	for (idx = 0; idx < devnum; idx++) {
		ret = rltk_wlan_start(idx);
		if (ret == 0) {
			_wifi_is_on = 1;
		}
		if (ret < 0) {
			RTW_API_INFO("\n\rERROR: Start WIFI Failed!");
			rltk_wlan_deinit();
			wifi_mode = RTW_MODE_NONE;
			device_mutex_unlock(RT_DEV_LOCK_WLAN);
			return ret;
		}
	}
	device_mutex_unlock(RT_DEV_LOCK_WLAN);

	while (1) {
		if (rltk_wlan_running(devnum - 1)) {
			RTW_API_INFO("\n\rWIFI initialized\n");
			wifi_set_country_code();
			break;
		}

		if (timeout == 0) {
			RTW_API_INFO("\n\rERROR: Init WIFI timeout!");
			break;
		}

		rtw_msleep_os(1000);
		timeout --;
	}

#if CONFIG_LWIP_LAYER
#if defined(CONFIG_MBED_ENABLED) || defined(CONFIG_PLATFOMR_CUSTOMER_RTOS)
	//TODO
#else
	netif_set_up(&xnetif[0]);
#if LWIP_VERSION_MAJOR >= 2 && LWIP_VERSION_MINOR >= 1
#if LWIP_IPV6
	netif_create_ip6_linklocal_address(&xnetif[0], 1);
#endif
#endif
	if (mode == RTW_MODE_AP) {
		netif_set_link_up(&xnetif[0]);
	} else	 if (mode == RTW_MODE_STA_AP) {
		netif_set_up(&xnetif[1]);
		netif_set_link_up(&xnetif[1]);
	}
#endif
#endif

#if CONFIG_INIC_EN
	inic_start();
#endif

	return ret;
}

int wifi_off(void)
{
	int ret = 0;
	int timeout = 20;

	if ((rltk_wlan_running(WLAN0_IDX) == 0) &&
		(rltk_wlan_running(WLAN1_IDX) == 0)) {
		RTW_API_INFO("\n\rWIFI is not running");
		return 0;
	}
#if CONFIG_LWIP_LAYER
#if defined(CONFIG_MBED_ENABLED) || defined(CONFIG_PLATFOMR_CUSTOMER_RTOS)
	//TODO
#else
	dhcps_deinit();
	LwIP_DHCP(0, DHCP_STOP);
#if LWIP_VERSION_MAJOR >= 2 && LWIP_VERSION_MINOR >= 1
#if LWIP_IPV6 && LWIP_IPV6_DHCP6
	LwIP_DHCP6(0, DHCP6_STOP);
#endif
#endif
	netif_set_down(&xnetif[0]);
	netif_set_down(&xnetif[1]);
#endif
#endif
#if defined(CONFIG_ENABLE_WPS_AP) && CONFIG_ENABLE_WPS_AP
	if ((wifi_mode ==  RTW_MODE_AP) || (wifi_mode == RTW_MODE_STA_AP)) {
		wpas_wps_deinit();
	}
#endif
	RTW_API_INFO("\n\rDeinitializing WIFI ...");
	device_mutex_lock(RT_DEV_LOCK_WLAN);
	rltk_wlan_deinit();
	_wifi_is_on = 0;
	device_mutex_unlock(RT_DEV_LOCK_WLAN);

	while (1) {
		if ((rltk_wlan_running(WLAN0_IDX) == 0) &&
			(rltk_wlan_running(WLAN1_IDX) == 0)) {
			RTW_API_INFO("\n\rWIFI deinitialized");
			break;
		}

		if (timeout == 0) {
			RTW_API_INFO("\n\rERROR: Deinit WIFI timeout!");
			ret = -1;
			break;
		}

		rtw_msleep_os(1000);
		timeout --;
	}

	wifi_mode = RTW_MODE_NONE;

#if CONFIG_INIC_EN
	inic_stop();
#endif

	return ret;
}

int wifi_off_fastly(void)
{
#if CONFIG_LWIP_LAYER
#if defined(CONFIG_MBED_ENABLED) || defined(CONFIG_PLATFOMR_CUSTOMER_RTOS)
	//TODO
#else
	dhcps_deinit();
	LwIP_DHCP(0, DHCP_STOP);
#if LWIP_VERSION_MAJOR >= 2 && LWIP_VERSION_MINOR >= 1
#if LWIP_IPV6 && LWIP_IPV6_DHCP6
	LwIP_DHCP6(0, DHCP6_STOP);
#endif
#endif
#endif
#endif
	//RTW_API_INFO("\n\rDeinitializing WIFI ...");
	device_mutex_lock(RT_DEV_LOCK_WLAN);
	rltk_wlan_deinit_fastly();
	device_mutex_unlock(RT_DEV_LOCK_WLAN);
	return 0;
}


int wifi_set_mode(rtw_mode_t mode)
{
	int ret = 0;
#ifdef CONFIG_WLAN_SWITCH_MODE
	rtw_mode_t curr_mode, next_mode;
#if defined(CONFIG_AUTO_RECONNECT) && CONFIG_AUTO_RECONNECT
	u8 autoreconnect_mode = 0;
#endif
#endif
	device_mutex_lock(RT_DEV_LOCK_WLAN);

	if ((rltk_wlan_running(WLAN0_IDX) == 0) &&
		(rltk_wlan_running(WLAN1_IDX) == 0)) {
		RTW_API_INFO("\n\r[%s] WIFI is not running", __FUNCTION__);
		device_mutex_unlock(RT_DEV_LOCK_WLAN);
		return -1;
	}

#ifdef CONFIG_WLAN_SWITCH_MODE
#if defined(CONFIG_AUTO_RECONNECT) && CONFIG_AUTO_RECONNECT
	wifi_get_autoreconnect(&autoreconnect_mode);
	if (autoreconnect_mode != RTW_AUTORECONNECT_DISABLE) {
		wifi_set_autoreconnect(RTW_AUTORECONNECT_DISABLE);

		// if set to AP mode, delay until the autoconnect task is finished
		if ((mode == RTW_MODE_AP) || (mode == RTW_MODE_STA_AP)) {
			while (param_indicator != NULL) {
				rtw_msleep_os(2);
			}
		}
	}
#endif
	curr_mode = wifi_mode;
	next_mode = mode;
	ret = rltk_set_mode_prehandle(curr_mode, next_mode, WLAN0_NAME);
	if (ret < 0) {
		goto Exit;
	}
#endif

	if ((wifi_mode == RTW_MODE_STA) && (mode == RTW_MODE_AP)) {
		RTW_API_INFO("\n\r[%s] WIFI Mode Change: STA-->AP", __FUNCTION__);

		wifi_disconnect();
		//must add this delay, because this API may have higher priority, wifi_disconnect will rely RTW_CMD task, may not be excuted immediately.
		rtw_msleep_os(50);

#if CONFIG_LWIP_LAYER
		netif_set_link_up(&xnetif[0]);
#endif

		wifi_mode = mode;
#ifdef CONFIG_PMKSA_CACHING
		wifi_set_pmk_cache_enable(0);
#endif
	} else if ((wifi_mode == RTW_MODE_AP) && (mode == RTW_MODE_STA)) {
		RTW_API_INFO("\n\r[%s] WIFI Mode Change: AP-->STA", __FUNCTION__);

		ret = wext_set_mode(WLAN0_NAME, IW_MODE_INFRA);
		if (ret < 0) {
			goto Exit;
		}

		rtw_msleep_os(50);

#if CONFIG_LWIP_LAYER
		netif_set_link_down(&xnetif[0]);
#endif

		wifi_mode = mode;
#ifdef CONFIG_PMKSA_CACHING
		wifi_set_pmk_cache_enable(1);
#endif
	} else if ((wifi_mode == RTW_MODE_AP) && (mode == RTW_MODE_AP)) {
		RTW_API_INFO("\n\rWIFI Mode Change: AP-->AP");
		ret = wext_set_mode(WLAN0_NAME, IW_MODE_INFRA);
		if (ret < 0) {
			goto Exit;
		}

		vTaskDelay(50);

	} else if ((wifi_mode == RTW_MODE_STA) && (mode == RTW_MODE_STA)) {
		RTW_API_INFO("\n\rWIFI Mode No Need To Change: STA -->STA");
	} else if ((wifi_mode == RTW_MODE_STA) && (mode == RTW_MODE_PROMISC)) {
		RTW_API_INFO("\n\rWIFI Mode Change: STA-->PROMISC");
		unsigned char ssid[33];
		if (wext_get_ssid(WLAN0_NAME, ssid) > 0) {
			wifi_disconnect();
		}
	} else if ((wifi_mode == RTW_MODE_AP) && (mode == RTW_MODE_PROMISC)) {
		RTW_API_INFO("\n\rWIFI Mode Change: AP-->PROMISC");//Same as AP--> STA
		ret = wext_set_mode(WLAN0_NAME, IW_MODE_INFRA);
		if (ret < 0) {
			goto Exit;
		}
		rtw_msleep_os(50);
#if CONFIG_LWIP_LAYER
		netif_set_link_down(&xnetif[0]);
#endif
		wifi_mode = mode;
	}
#ifdef CONFIG_CONCURRENT_MODE
#ifdef CONFIG_WLAN_SWITCH_MODE
	else if ((wifi_mode == RTW_MODE_STA_AP) && (mode == RTW_MODE_STA)) {
		RTW_API_INFO("\n\rWIFI Mode Change: CONCURRENT-->STA");
#if CONFIG_LWIP_LAYER
		dhcps_deinit();
		netif_set_down(&xnetif[1]);
		netif_set_link_down(&xnetif[1]);
#endif
	} else if ((wifi_mode == RTW_MODE_STA) && (mode == RTW_MODE_STA_AP)) {
		RTW_API_INFO("\n\rWIFI Mode Change: STA-->CONCURRENT");
#if CONFIG_LWIP_LAYER
		dhcps_init(&xnetif[1]);
		netif_set_up(&xnetif[1]);
		netif_set_link_up(&xnetif[1]);
#endif
		wifi_mode = mode;
	}
#endif
#endif
	else {
		RTW_API_INFO("\n\rWIFI Mode Change: not support");
		goto Exit;
	}

#ifdef CONFIG_WLAN_SWITCH_MODE
	ret = rltk_set_mode_posthandle(curr_mode, next_mode, WLAN0_NAME);
	if (ret < 0) {
		goto Exit;
	}
#ifdef CONFIG_CONCURRENT_MODE
	if ((wifi_mode == RTW_MODE_STA_AP) && (mode == RTW_MODE_STA)) {
		wifi_mode = RTW_MODE_STA;
	}
#endif
#if defined(CONFIG_AUTO_RECONNECT) && CONFIG_AUTO_RECONNECT
	/* enable auto reconnect */
	if (autoreconnect_mode != RTW_AUTORECONNECT_DISABLE) {
		wifi_set_autoreconnect(autoreconnect_mode);
	}
#endif
#endif

	device_mutex_unlock(RT_DEV_LOCK_WLAN);
	return 0;

Exit:
#ifdef CONFIG_WLAN_SWITCH_MODE
#if defined(CONFIG_AUTO_RECONNECT) && CONFIG_AUTO_RECONNECT
	/* enable auto reconnect */
	if (autoreconnect_mode != RTW_AUTORECONNECT_DISABLE) {
		wifi_set_autoreconnect(autoreconnect_mode);
	}
#endif
#endif
	device_mutex_unlock(RT_DEV_LOCK_WLAN);
	return -1;
}

int wifi_set_wpa_mode(rtw_wpa_mode wpa_mode)
{
	if (wpa_mode > WPA2_WPA3_MIXED_MODE) {
		return -1;
	} else {
		wifi_wpa_mode = wpa_mode;
		return 0;
	}
}

int wifi_set_power_mode(unsigned char ips_mode, unsigned char lps_mode)
{
	return wext_enable_powersave(WLAN0_NAME, ips_mode, lps_mode);
}

int wifi_set_tdma_param(unsigned char slot_period, unsigned char rfon_period_len_1, unsigned char rfon_period_len_2, unsigned char rfon_period_len_3)
{
	return wext_set_tdma_param(WLAN0_NAME, slot_period, rfon_period_len_1, rfon_period_len_2, rfon_period_len_3);
}

int wifi_set_lps_dtim(unsigned char dtim)
{
	return wext_set_lps_dtim(WLAN0_NAME, dtim);
}

int wifi_get_lps_dtim(unsigned char *dtim)
{
	return wext_get_lps_dtim(WLAN0_NAME, dtim);
}

// mode == 0: packet count, 1: enter directly, 2: tp threshold (default)
int wifi_set_lps_thresh(rtw_lps_thresh_t mode)
{
	return wext_set_lps_thresh(WLAN0_NAME, mode);
}

int wifi_set_beacon_mode(int mode)
{
	return wext_set_beacon_mode(WLAN0_NAME, mode);
}

int wifi_set_lps_level(unsigned char lps_level)
{
	return wext_set_lps_level(WLAN0_NAME, lps_level);
}

//request wifi driver close RF immediatly
int wifi_radio_off_directly(void)
{
	return wext_close_lps_rf(WLAN0_NAME);
}

//----------------------------------------------------------------------------//
static void wifi_ap_sta_assoc_hdl(char *buf, int buf_len, int flags, void *userdata)
{
	/* To avoid gcc warnings */
	(void) buf;
	(void) buf_len;
	(void) flags;
	(void) userdata;
	//USER TODO

}
static void wifi_ap_sta_disassoc_hdl(char *buf, int buf_len, int flags, void *userdata)
{
	/* To avoid gcc warnings */
	(void) buf;
	(void) buf_len;
	(void) flags;
	(void) userdata;
	//USER TODO
}

int wifi_get_last_error(void)
{
	return error_flag;
}


#if defined(CONFIG_ENABLE_WPS_AP) && CONFIG_ENABLE_WPS_AP
int wpas_wps_init(const char *ifname);
#endif

int wifi_set_mfp_support(unsigned char value)
{
	return wext_set_mfp_support(WLAN0_NAME, value);
}

#ifdef CONFIG_SAE_SUPPORT
int wifi_set_group_id(unsigned char value)
{
	return wext_set_group_id(WLAN0_NAME, value);
}
#endif

#ifdef CONFIG_PMKSA_CACHING
int wifi_set_pmk_cache_enable(unsigned char value)
{
	return wext_set_pmk_cache_enable(WLAN0_NAME, value);
}
#endif

int wifi_start_ap(
	char 				*ssid,
	rtw_security_t		security_type,
	char 				*password,
	int 				ssid_len,
	int 				password_len,
	int					channel)
{
	const char *ifname = WLAN0_NAME;
	int ret = 0;

	if ((ssid_len < RTW_MIN_SSID_LEN) || (ssid_len > RTW_MAX_SSID_LEN)) {
		printf("Error: SSID should be 0-32 characters\r\n");
		ret = RTW_BADARG;
		goto exit;
	}

	if (password == NULL) {
		if (security_type != RTW_SECURITY_OPEN) {
			ret = RTW_INVALID_KEY;
			goto exit;
		}
	}
	if (security_type != RTW_SECURITY_OPEN) {
		if (password_len <= RTW_WPA2_MAX_PSK_LEN &&
			password_len >= RTW_MIN_PSK_LEN) {
			if (password_len == RTW_WPA2_MAX_PSK_LEN) { //password_len=64 means pre-shared key, pre-shared key should be 64 hex characters
				unsigned char i, j;
				for (i = 0; i < RTW_WPA2_MAX_PSK_LEN; i++) {
					j = password[i];
					if (!((j >= '0' && j <= '9') || (j >= 'A' && j <= 'F') || (j >= 'a' && j <= 'f'))) {
						printf("Error: password should be 64 hex characters or 8-63 ASCII characters\n\r");
						ret = RTW_INVALID_KEY;
						goto exit;
					}
				}
			}
		}
#ifdef CONFIG_FPGA
		else if ((password_len == 5) && (security_type == RTW_SECURITY_WEP_PSK)) {
		}
#endif
		else {
			printf("Error: password should be 64 hex characters or 8-63 ASCII characters\n\r");
			ret = RTW_INVALID_KEY;
			goto exit;
		}
	}

#if defined (CONFIG_AP_MODE) && defined (CONFIG_NATIVEAP_MLME)
	if (wifi_mode == RTW_MODE_STA_AP) {
		ifname = WLAN1_NAME;
	}

	if (is_promisc_enabled()) {
		promisc_set(RTW_PROMISC_DISABLE, NULL, 0);
	}

	wifi_reg_event_handler(WIFI_EVENT_STA_ASSOC, wifi_ap_sta_assoc_hdl, NULL);
	wifi_reg_event_handler(WIFI_EVENT_STA_DISASSOC, wifi_ap_sta_disassoc_hdl, NULL);

	ret = wext_set_mode(ifname, IW_MODE_MASTER);
	if (ret < 0) {
		goto exit;
	}
	ret = wext_set_channel(ifname, channel);	//Set channel before starting ap
	if (ret < 0) {
		goto exit;
	}

	switch (security_type) {
	case RTW_SECURITY_OPEN:
		break;
#if defined(CONFIG_FPGA) && CONFIG_FPGA
	case RTW_SECURITY_WEP_PSK:
		ret = wext_set_auth_param(ifname, IW_AUTH_80211_AUTH_ALG, IW_AUTH_ALG_OPEN_SYSTEM);
		if (ret == 0) {
			ret = wext_set_key_ext(ifname, IW_ENCODE_ALG_WEP, NULL, 0, 1, 0, 0, (u8 *)password, password_len);
		}
		break;
	case RTW_SECURITY_WPA2_TKIP_PSK:
		ret = wext_set_auth_param(ifname, IW_AUTH_80211_AUTH_ALG, IW_AUTH_ALG_OPEN_SYSTEM);
		if (ret == 0) {
			ret = wext_set_key_ext(ifname, IW_ENCODE_ALG_TKIP, NULL, 0, 0, 0, 0, NULL, 0);
		}
		if (ret == 0) {
			ret = wext_set_passphrase(ifname, (u8 *)password, password_len);
		}
		break;
#endif
	case RTW_SECURITY_WPA2_AES_PSK:
		ret = wext_set_auth_param(ifname, IW_AUTH_80211_AUTH_ALG, IW_AUTH_ALG_OPEN_SYSTEM);
		if (ret == 0) {
			ret = wext_set_key_ext(ifname, IW_ENCODE_ALG_CCMP, NULL, 0, 0, 0, 0, NULL, 0);
		}
		if (ret == 0) {
			ret = wext_set_passphrase(ifname, (u8 *)password, password_len);
		}
		break;
#ifdef CONFIG_IEEE80211W
	case RTW_SECURITY_WPA2_AES_CMAC:
		ret = wext_set_auth_param(ifname, IW_AUTH_80211_AUTH_ALG, IW_AUTH_ALG_OPEN_SYSTEM);
		if (ret == 0) {
			ret = wext_set_key_ext(ifname, IW_ENCODE_ALG_AES_CMAC, NULL, 0, 0, 0, 0, NULL, 0);
		}
		if (ret == 0) {
			ret = wext_set_passphrase(ifname, (u8 *)password, password_len);
		}
		break;
#endif
	default:
		ret = -1;
		RTW_API_INFO("\n\rWIFICONF: security type is not supported");
		break;
	}
	if (ret < 0) {
		goto exit;
	}

	ret = wext_set_ap_ssid(ifname, (u8 *)ssid, ssid_len);
#if defined(CONFIG_ENABLE_WPS_AP) && CONFIG_ENABLE_WPS_AP
	wpas_wps_init(ifname);
#endif
#if CONFIG_LWIP_LAYER
#if defined(CONFIG_MBED_ENABLED) || defined(CONFIG_PLATFOMR_CUSTOMER_RTOS)
	//TODO
#else
	if (wifi_mode == RTW_MODE_STA_AP) {
		netif_set_link_up(&xnetif[1]);
	} else {
		netif_set_link_up(&xnetif[0]);
	}
#endif
#endif
exit:
#endif
	return ret;
}

extern int set_hidden_ssid(const char *ifname, u8 value);
int wifi_start_ap_with_hidden_ssid(
	char 				*ssid,
	rtw_security_t		security_type,
	char 				*password,
	int 				ssid_len,
	int 				password_len,
	int					channel)
{
	const char *ifname = WLAN0_NAME;
	int ret = 0;

	if ((ssid_len < RTW_MIN_SSID_LEN) || (ssid_len > RTW_MAX_SSID_LEN)) {
		printf("Error: SSID should be 0-32 characters\r\n");
		ret = RTW_BADARG;
		goto exit;
	}

	if (password == NULL) {
		if (security_type != RTW_SECURITY_OPEN) {
			ret = RTW_INVALID_KEY;
			goto exit;
		}
	}
	if (security_type != RTW_SECURITY_OPEN) {
		if (password_len <= RTW_WPA2_MAX_PSK_LEN &&
			password_len >= RTW_MIN_PSK_LEN) {
			if (password_len == RTW_WPA2_MAX_PSK_LEN) { //password_len=64 means pre-shared key, pre-shared key should be 64 hex characters
				unsigned char i, j;
				for (i = 0; i < RTW_WPA2_MAX_PSK_LEN; i++) {
					j = password[i];
					if (!((j >= '0' && j <= '9') || (j >= 'A' && j <= 'F') || (j >= 'a' && j <= 'f'))) {
						printf("Error: password should be 64 hex characters or 8-63 ASCII characters\n\r");
						ret = RTW_INVALID_KEY;
						goto exit;
					}
				}
			}
		}
#ifdef CONFIG_FPGA
		else if ((password_len == 5) && (security_type == RTW_SECURITY_WEP_PSK)) {
		}
#endif
		else {
			printf("Error: password should be 64 hex characters or 8-63 ASCII characters\n\r");
			ret = RTW_INVALID_KEY;
			goto exit;
		}
	}

#if defined (CONFIG_AP_MODE) && defined (CONFIG_NATIVEAP_MLME)
	if (wifi_mode == RTW_MODE_STA_AP) {
		ifname = WLAN1_NAME;
	}

	if (is_promisc_enabled()) {
		promisc_set(RTW_PROMISC_DISABLE, NULL, 0);
	}

	wifi_reg_event_handler(WIFI_EVENT_STA_ASSOC, wifi_ap_sta_assoc_hdl, NULL);
	wifi_reg_event_handler(WIFI_EVENT_STA_DISASSOC, wifi_ap_sta_disassoc_hdl, NULL);

	ret = wext_set_mode(ifname, IW_MODE_MASTER);
	if (ret < 0) {
		goto exit;
	}
	ret = wext_set_channel(ifname, channel);	//Set channel before starting ap
	if (ret < 0) {
		goto exit;
	}

	switch (security_type) {
	case RTW_SECURITY_OPEN:
		break;
	case RTW_SECURITY_WPA2_AES_PSK:
		ret = wext_set_auth_param(ifname, IW_AUTH_80211_AUTH_ALG, IW_AUTH_ALG_OPEN_SYSTEM);
		if (ret == 0) {
			ret = wext_set_key_ext(ifname, IW_ENCODE_ALG_CCMP, NULL, 0, 0, 0, 0, NULL, 0);
		}
		if (ret == 0) {
			ret = wext_set_passphrase(ifname, (u8 *)password, password_len);
		}
		break;
	default:
		ret = -1;
		RTW_API_INFO("\n\rWIFICONF: security type is not supported");
		break;
	}
	if (ret < 0) {
		goto exit;
	}

	ret = set_hidden_ssid(ifname, 1);
	if (ret < 0) {
		goto exit;
	}

	ret = wext_set_ap_ssid(ifname, (u8 *)ssid, ssid_len);
#if defined(CONFIG_ENABLE_WPS_AP) && CONFIG_ENABLE_WPS_AP
	wpas_wps_init(ifname);
#endif
exit:
#endif
	return ret;
}

void wifi_scan_each_report_hdl(char *buf, int buf_len, int flags, void *userdata)
{
	/* To avoid gcc warnings */
	(void) buf_len;
	(void) flags;
	(void) userdata;

	int i = 0;
	int j = 0;
	int insert_pos = 0;
	rtw_scan_result_t **result_ptr = (rtw_scan_result_t **)buf;
	rtw_scan_result_t *temp = NULL;
	/* buf detail: scan buffer/rtw_scan_result_t, buf_len = size of rtw_scan_result_t structure*/

	for (i = 0; i < scan_result_handler_ptr.scan_cnt; i++) {
		if (CMP_MAC(scan_result_handler_ptr.pap_details[i]->BSSID.octet, (*result_ptr)->BSSID.octet)) {
			if ((*result_ptr)->signal_strength > scan_result_handler_ptr.pap_details[i]->signal_strength) {
				temp = scan_result_handler_ptr.pap_details[i];
				for (j = i - 1; j >= 0; j--) {
					if (scan_result_handler_ptr.pap_details[j]->signal_strength >= (*result_ptr)->signal_strength) {
						break;
					} else {
						scan_result_handler_ptr.pap_details[j + 1] = scan_result_handler_ptr.pap_details[j];
					}
				}
				scan_result_handler_ptr.pap_details[j + 1] = temp;
				scan_result_handler_ptr.pap_details[j + 1]->signal_strength = (*result_ptr)->signal_strength;
			}
			memset(*result_ptr, 0, sizeof(rtw_scan_result_t));
			return;
		}
	}

	//scan_result_handler_ptr.scan_cnt++;

	if (scan_result_handler_ptr.scan_cnt >= scan_result_handler_ptr.max_ap_size) {
		scan_result_handler_ptr.scan_cnt = scan_result_handler_ptr.max_ap_size;
		if ((*result_ptr)->signal_strength > scan_result_handler_ptr.pap_details[scan_result_handler_ptr.max_ap_size - 1]->signal_strength) {
			rtw_memcpy(scan_result_handler_ptr.pap_details[scan_result_handler_ptr.max_ap_size - 1], *result_ptr, sizeof(rtw_scan_result_t));
			temp = scan_result_handler_ptr.pap_details[scan_result_handler_ptr.max_ap_size - 1];
			scan_result_handler_ptr.scan_cnt  = scan_result_handler_ptr.max_ap_size - 1;
		} else {
			return;
		}
	} else {
		rtw_memcpy(&scan_result_handler_ptr.ap_details[scan_result_handler_ptr.scan_cnt], *result_ptr, sizeof(rtw_scan_result_t));
	}

	for (i = 0; i < scan_result_handler_ptr.scan_cnt; i++) {
		if ((*result_ptr)->signal_strength > scan_result_handler_ptr.pap_details[i]->signal_strength) {
			break;
		}
	}
	insert_pos = i;

	for (i = scan_result_handler_ptr.scan_cnt; i > insert_pos; i--) {
		scan_result_handler_ptr.pap_details[i] = scan_result_handler_ptr.pap_details[i - 1];
	}

	if (temp != NULL) {
		scan_result_handler_ptr.pap_details[insert_pos] = temp;
	} else {
		scan_result_handler_ptr.pap_details[insert_pos] = &scan_result_handler_ptr.ap_details[scan_result_handler_ptr.scan_cnt];
	}

	if (scan_result_handler_ptr.scan_cnt < scan_result_handler_ptr.max_ap_size) {
		scan_result_handler_ptr.scan_cnt++;
	}

	rtw_memset(*result_ptr, 0, sizeof(rtw_scan_result_t));
}

void wifi_scan_done_hdl(char *buf, int buf_len, int flags, void *userdata)
{
	/* To avoid gcc warnings */
	(void) buf;
	(void) buf_len;
	(void) flags;
	(void) userdata;

	int i = 0;
	rtw_scan_handler_result_t scan_result_report;

	for (i = 0; i < scan_result_handler_ptr.scan_cnt; i++) {
		rtw_memcpy(&scan_result_report.ap_details, scan_result_handler_ptr.pap_details[i], sizeof(rtw_scan_result_t));
		scan_result_report.scan_complete = scan_result_handler_ptr.scan_complete;
		scan_result_report.user_data = scan_result_handler_ptr.user_data;
		(*scan_result_handler_ptr.gscan_result_handler)(&scan_result_report);
	}

	scan_result_handler_ptr.scan_complete = RTW_TRUE;
	scan_result_report.scan_complete = RTW_TRUE;
	scan_result_report.user_data = scan_result_handler_ptr.user_data;
	(*scan_result_handler_ptr.gscan_result_handler)(&scan_result_report);

	rtw_free(scan_result_handler_ptr.ap_details);
	rtw_free(scan_result_handler_ptr.pap_details);
#if SCAN_USE_SEMAPHORE
	rtw_up_sema(&scan_result_handler_ptr.scan_semaphore);
#else
	scan_result_handler_ptr.scan_running = 0;
#endif
	wifi_unreg_event_handler(WIFI_EVENT_SCAN_RESULT_REPORT, wifi_scan_each_report_hdl);
	wifi_unreg_event_handler(WIFI_EVENT_SCAN_DONE, wifi_scan_done_hdl);
	return;
}

void wifi_scan_done_hdl_mcc(char *buf, int buf_len, int flags, void *userdata)
{
#if SCAN_USE_SEMAPHORE
	rtw_up_sema(&scan_result_handler_ptr.scan_semaphore);
#else
	scan_result_handler_ptr.scan_running = 0;
#endif
	return;
}

//int rtk_wifi_scan(char *buf, int buf_len, xSemaphoreHandle * semaphore)
int wifi_scan(rtw_scan_type_t                    scan_type,
			  rtw_bss_type_t                     bss_type,
			  void                *result_ptr)
{
	int ret;
	scan_buf_arg *pscan_buf;
	u16 flags = scan_type | (bss_type << 8);
	if (result_ptr != NULL) {
		pscan_buf = (scan_buf_arg *)result_ptr;
		ret = wext_set_scan(scan_interface, (char *)pscan_buf->buf, pscan_buf->buf_len, flags);
	} else {
		wifi_reg_event_handler(WIFI_EVENT_SCAN_RESULT_REPORT, wifi_scan_each_report_hdl, NULL);
		wifi_reg_event_handler(WIFI_EVENT_SCAN_DONE, wifi_scan_done_hdl, NULL);
		ret = wext_set_scan(scan_interface, NULL, 0, flags);
	}

	if (ret == 0) {
		if (result_ptr != NULL) {
			ret = wext_get_scan(scan_interface, pscan_buf->buf, pscan_buf->buf_len);
		}
	} else if (ret == -1) {
		if (result_ptr == NULL) {
			wifi_unreg_event_handler(WIFI_EVENT_SCAN_RESULT_REPORT, wifi_scan_each_report_hdl);
			wifi_unreg_event_handler(WIFI_EVENT_SCAN_DONE, wifi_scan_done_hdl);
		}
	}
	return ret;
}

int wifi_scan_networks_with_ssid(int (results_handler)(char *buf, int buflen, char *ssid, void *user_data),
								 OUT void *user_data, IN int scan_buflen, IN char *ssid, IN int ssid_len)
{
	int scan_cnt = 0, add_cnt = 0;
	scan_buf_arg scan_buf;
	int ret;

	scan_buf.buf_len = scan_buflen;
	scan_buf.buf = (char *)rtw_malloc(scan_buf.buf_len);
	if (!scan_buf.buf) {
		RTW_API_INFO("\n\rERROR: Can't malloc memory(%d)", scan_buf.buf_len);
		return RTW_NOMEM;
	}
	//set ssid
	memset(scan_buf.buf, 0, scan_buf.buf_len);
	memcpy(scan_buf.buf, &ssid_len, sizeof(int));
	memcpy(scan_buf.buf + sizeof(int), ssid, ssid_len);

	//Scan channel
	if ((scan_cnt = wifi_scan(RTW_SCAN_TYPE_ACTIVE, RTW_BSS_TYPE_ANY, &scan_buf)) < 0) {
		RTW_API_INFO("\n\rERROR: wifi scan failed");
		ret = RTW_ERROR;
	} else {
		if (NULL == results_handler) {
			int plen = 0;
			while (plen < scan_buf.buf_len) {
				int len, rssi, ssid_len, i, security_mode;
				int wps_password_id;
				char *mac, *ssid;
				//u8 *security_mode;
				RTW_API_INFO("\n\r");
				// len
				len = (int) * (scan_buf.buf + plen);
				RTW_API_INFO("len = %d,\t", len);
				// check end
				if (len == 0) {
					break;
				}
				// mac
				mac = scan_buf.buf + plen + 1;
				RTW_API_INFO("mac = ");
				for (i = 0; i < 6; i++) {
					RTW_API_INFO("%02x ", (u8) * (mac + i));
				}
				RTW_API_INFO(",\t");
				// rssi
				rssi = *(int *)(scan_buf.buf + plen + 1 + 6);
				RTW_API_INFO(" rssi = %d,\t", rssi);
				// security_mode
				security_mode = (int) * (scan_buf.buf + plen + 1 + 6 + 4);
				switch (security_mode) {
				case IW_ENCODE_ALG_NONE:
					RTW_API_INFO("sec = open    ,\t");
					break;
				case IW_ENCODE_ALG_WEP:
					RTW_API_INFO("sec = wep     ,\t");
					break;
				case IW_ENCODE_ALG_CCMP:
					RTW_API_INFO("sec = wpa/wpa2,\t");
					break;
				}
				// password id
				wps_password_id = (int) * (scan_buf.buf + plen + 1 + 6 + 4 + 1);
				RTW_API_INFO("wps password id = %d,\t", wps_password_id);

				RTW_API_INFO("channel = %d,\t", *(scan_buf.buf + plen + 1 + 6 + 4 + 1 + 1));
				// ssid
				ssid_len = len - 1 - 6 - 4 - 1 - 1 - 1;
				ssid = scan_buf.buf + plen + 1 + 6 + 4 + 1 + 1 + 1;
				RTW_API_INFO("ssid = ");
				for (i = 0; i < ssid_len; i++) {
					RTW_API_INFO("%c", *(ssid + i));
				}
				plen += len;
				add_cnt++;
			}

			RTW_API_INFO("\n\rwifi_scan: add count = %d, scan count = %d", add_cnt, scan_cnt);
		}
		ret = RTW_SUCCESS;
	}
	if (results_handler) {
		results_handler(scan_buf.buf, scan_buf.buf_len, ssid, user_data);
	}

	if (scan_buf.buf) {
		rtw_free(scan_buf.buf);
	}

	return ret;
}

#define BUFLEN_LEN   1
#define MAC_LEN      6
#define RSSI_LEN     4
#define SECURITY_LEN 1
#define SECURITY_LEN_EXTENDED 4
#define WPS_ID_LEN   1
#define CHANNEL_LEN  1
#ifdef CONFIG_P2P_NEW
#define P2P_ROLE_LEN     1
#define P2P_CHANNEL_LEN  1
#define P2P_ROLE_DISABLE 0
#define P2P_ROLE_DEVICE  1
#define P2P_ROLE_CLIENT  2
#define P2P_ROLE_GO      3
#endif

int wifi_scan_networks_with_ssid_by_extended_security(int (results_handler)(char *buf, int buflen, char *ssid, void *user_data),
		OUT void *user_data, IN int scan_buflen, IN char *ssid, IN int ssid_len)
{
	int scan_cnt = 0, add_cnt = 0;
	scan_buf_arg scan_buf;
	int ret;

	scan_buf.buf_len = scan_buflen;
	scan_buf.buf = (char *)rtw_malloc(scan_buf.buf_len);
	if (!scan_buf.buf) {
		RTW_API_INFO("\n\rERROR: Can't malloc memory(%d)", scan_buf.buf_len);
		return RTW_NOMEM;
	}

	rltk_wlan_enable_scan_with_ssid_by_extended_security(1);

	//set ssid
	memset(scan_buf.buf, 0, scan_buf.buf_len);
	memcpy(scan_buf.buf, &ssid_len, sizeof(int));
	memcpy(scan_buf.buf + sizeof(int), ssid, ssid_len);

	//Scan channel
	if ((scan_cnt = wifi_scan(RTW_SCAN_TYPE_ACTIVE, RTW_BSS_TYPE_ANY, &scan_buf)) < 0) {
		RTW_API_INFO("\n\rERROR: wifi scan failed");
		ret = RTW_ERROR;
	} else {
		if (NULL == results_handler) {
			int plen = 0;
			while (plen < scan_buf.buf_len) {
				int len, rssi, ssid_len, i, security_mode;
				int wps_password_id;
				char *mac, *ssid;
#ifdef CONFIG_P2P_NEW
				int p2p_role, p2p_listen_channel;
				char *dev_name = NULL;
				int dev_name_len = 0;
#endif
				RTW_API_INFO("\n\r");
				// len
				len = (int) * (scan_buf.buf + plen);
				RTW_API_INFO("len = %d,\t", len);
				// check end
				if (len == 0) {
					break;
				}
				// mac
				mac = scan_buf.buf + plen + BUFLEN_LEN;
				RTW_API_INFO("mac = ");
				for (i = 0; i < 6; i++) {
					RTW_API_INFO("%02x ", (u8) * (mac + i));
				}
				RTW_API_INFO(",\t");
				// rssi
				rssi = *(int *)(scan_buf.buf + plen + BUFLEN_LEN + MAC_LEN);
				RTW_API_INFO(" rssi = %d,\t", rssi);
				// security_mode
				//get extended security flag is up, output detailed security infomation
				security_mode = *(int *)(scan_buf.buf + plen + BUFLEN_LEN + MAC_LEN + RSSI_LEN);
				switch (security_mode) {
				case RTW_SECURITY_OPEN:
					RTW_API_INFO("sec = RTW_SECURITY_OPEN	 ,\t");
					break;
				case RTW_SECURITY_WEP_PSK:
					RTW_API_INFO("sec = RTW_SECURITY_WEP_PSK	 ,\t");
					break;
				case RTW_SECURITY_WEP_SHARED:
					RTW_API_INFO("sec = RTW_SECURITY_WEP_SHARED,\t");
					break;
				case RTW_SECURITY_WPA_AES_PSK:
					RTW_API_INFO("sec = RTW_SECURITY_WPA_AES_PSK,\t");
					break;
				case RTW_SECURITY_WPA_TKIP_PSK:
					RTW_API_INFO("sec = RTW_SECURITY_WPA_TKIP_PSK,\t");
					break;
				case RTW_SECURITY_WPA_MIXED_PSK:
					RTW_API_INFO("sec = RTW_SECURITY_WPA_MIXED_PSK,\t");
					break;
				case RTW_SECURITY_WPA2_AES_PSK:
					RTW_API_INFO("sec = RTW_SECURITY_WPA2_AES_PSK,\t");
					break;
				case RTW_SECURITY_WPA2_TKIP_PSK:
					RTW_API_INFO("sec = RTW_SECURITY_WPA2_TKIP_PSK,\t");
					break;
				case RTW_SECURITY_WPA2_MIXED_PSK:
					RTW_API_INFO("sec = RTW_SECURITY_WPA2_MIXED_PSK,\t");
					break;
				case RTW_SECURITY_WPA_WPA2_AES_PSK:
					RTW_API_INFO("sec = RTW_SECURITY_WPA_WPA2_AES_PSK,\t");
					break;
				case RTW_SECURITY_WPA_WPA2_TKIP_PSK:
					RTW_API_INFO("sec = RTW_SECURITY_WPA_WPA2_TKIP_PSK,\t");
					break;
				case RTW_SECURITY_WPA_WPA2_MIXED_PSK:
					RTW_API_INFO("sec = RTW_SECURITY_WPA_WPA2_MIXED_PSK,\t");
					break;
				case RTW_SECURITY_WPA2_AES_CMAC:
					RTW_API_INFO("sec = RTW_SECURITY_WPA2_AES_CMAC,\t");
					break;
				case RTW_SECURITY_WPA_AES_ENTERPRISE:
					RTW_API_INFO("sec = RTW_SECURITY_WPA_AES_ENTERPRISE,\t");
					break;
				case RTW_SECURITY_WPA_TKIP_ENTERPRISE:
					RTW_API_INFO("sec = RTW_SECURITY_WPA_TKIP_ENTERPRISE,\t");
					break;
				case RTW_SECURITY_WPA_MIXED_ENTERPRISE:
					RTW_API_INFO("sec = RTW_SECURITY_WPA_MIXED_ENTERPRISE,\t");
					break;
				case RTW_SECURITY_WPA2_AES_ENTERPRISE:
					RTW_API_INFO("sec = RTW_SECURITY_WPA2_AES_ENTERPRISE,\t");
					break;
				case RTW_SECURITY_WPA2_TKIP_ENTERPRISE:
					RTW_API_INFO("sec = RTW_SECURITY_WPA2_TKIP_ENTERPRISE,\t");
					break;
				case RTW_SECURITY_WPA2_MIXED_ENTERPRISE:
					RTW_API_INFO("sec = RTW_SECURITY_WPA2_MIXED_ENTERPRISE,\t");
					break;
				case RTW_SECURITY_WPA_WPA2_AES_ENTERPRISE:
					RTW_API_INFO("sec = RTW_SECURITY_WPA_WPA2_AES_ENTERPRISE,\t");
					break;
				case RTW_SECURITY_WPA_WPA2_TKIP_ENTERPRISE:
					RTW_API_INFO("sec = RTW_SECURITY_WPA_WPA2_TKIP_ENTERPRISE,\t");
					break;
				case RTW_SECURITY_WPA_WPA2_MIXED_ENTERPRISE:
					RTW_API_INFO("sec = RTW_SECURITY_WPA_WPA2_MIXED_ENTERPRISE,\t");
					break;
				case RTW_SECURITY_WPS_OPEN:
					RTW_API_INFO("sec = RTW_SECURITY_WPS_OPEN,\t");
					break;
				case RTW_SECURITY_WPS_SECURE:
					RTW_API_INFO("sec = RTW_SECURITY_WPS_SECURE,\t");
					break;
				case RTW_SECURITY_WPA3_AES_PSK:
					RTW_API_INFO("sec = RTW_SECURITY_WPA3_AES_PSK,\t");
				case RTW_SECURITY_WPA2_WPA3_MIXED:
					RTW_API_INFO("sec = RTW_SECURITY_WPA2_WPA3_MIXED,\t");
					break;
				}
				// password id
				wps_password_id = (int) * (scan_buf.buf + plen + BUFLEN_LEN + MAC_LEN + RSSI_LEN + SECURITY_LEN_EXTENDED);
				RTW_API_INFO("wps password id = %d,\t", wps_password_id);
#ifdef CONFIG_P2P_NEW
				if (wifi_mode == RTW_MODE_P2P) {
					p2p_role = (int) * (scan_buf.buf + plen + BUFLEN_LEN + MAC_LEN + RSSI_LEN + SECURITY_LEN_EXTENDED + WPS_ID_LEN);
					switch (p2p_role) {
					case P2P_ROLE_DISABLE:
						RTW_API_INFO("p2p role = P2P_ROLE_DISABLE,\t");
						break;
					case P2P_ROLE_DEVICE:
						RTW_API_INFO("p2p role = P2P_ROLE_DEVICE,\t");
						break;
					case P2P_ROLE_CLIENT:
						RTW_API_INFO("p2p role = P2P_ROLE_CLIENT,\t");
						break;
					case P2P_ROLE_GO:
						RTW_API_INFO("p2p role = P2P_ROLE_GO,\t");
						break;
					}
					p2p_listen_channel = (int) * (scan_buf.buf + plen + BUFLEN_LEN + MAC_LEN + RSSI_LEN + SECURITY_LEN_EXTENDED + WPS_ID_LEN + P2P_ROLE_LEN);
					RTW_API_INFO("p2p listen channel = %d,\t", p2p_listen_channel);

					if (p2p_role == P2P_ROLE_DEVICE) {
						//device name
						dev_name_len = len - BUFLEN_LEN - MAC_LEN - RSSI_LEN - SECURITY_LEN_EXTENDED - WPS_ID_LEN - P2P_ROLE_LEN - P2P_CHANNEL_LEN;
						dev_name = scan_buf.buf + plen + BUFLEN_LEN + MAC_LEN + RSSI_LEN + SECURITY_LEN_EXTENDED + P2P_ROLE_LEN + P2P_CHANNEL_LEN;
						RTW_API_INFO("dev_name = ");
						for (i = 0; i < dev_name_len; i++) {
							RTW_API_INFO("%c", *(dev_name + i));
						}
					} else {
						//ssid
						ssid_len = len - BUFLEN_LEN - MAC_LEN - RSSI_LEN - SECURITY_LEN_EXTENDED - WPS_ID_LEN - P2P_ROLE_LEN - P2P_CHANNEL_LEN;
						ssid = scan_buf.buf + plen + BUFLEN_LEN + MAC_LEN + RSSI_LEN + SECURITY_LEN_EXTENDED + P2P_ROLE_LEN + P2P_CHANNEL_LEN;
						RTW_API_INFO("ssid = ");
						for (i = 0; i < ssid_len; i++) {
							RTW_API_INFO("%c", *(ssid + i));
						}
					}
				} else
#endif //CONFIG_P2P_NEW
				{
					RTW_API_INFO("channel = %d,\t", *(scan_buf.buf + plen + BUFLEN_LEN + MAC_LEN + RSSI_LEN + SECURITY_LEN_EXTENDED + WPS_ID_LEN));
					// ssid
					ssid_len = len - BUFLEN_LEN - MAC_LEN - RSSI_LEN - SECURITY_LEN_EXTENDED - WPS_ID_LEN - CHANNEL_LEN;
					ssid = scan_buf.buf + plen + BUFLEN_LEN + MAC_LEN + RSSI_LEN + SECURITY_LEN_EXTENDED + WPS_ID_LEN + CHANNEL_LEN;
					RTW_API_INFO("ssid = ");
					for (i = 0; i < ssid_len; i++) {
						RTW_API_INFO("%c", *(ssid + i));
					}
				}
				plen += len;
				add_cnt++;
			}

			RTW_API_INFO("\n\rwifi_scan: add count = %d, scan count = %d", add_cnt, scan_cnt);
		}
		ret = RTW_SUCCESS;
	}
	if (results_handler) {
		results_handler(scan_buf.buf, scan_buf.buf_len, ssid, user_data);
	}

	if (scan_buf.buf) {
		rtw_free(scan_buf.buf);
	}

	return ret;
}

int wifi_scan_networks(rtw_scan_result_handler_t results_handler, void *user_data)
{
	unsigned int max_ap_size = 64;

	/* lock 2s to forbid suspend under scan */
	rtw_wakelock_timeout(2 * 1000);

#if SCAN_USE_SEMAPHORE
	rtw_bool_t result;
	if (NULL == scan_result_handler_ptr.scan_semaphore) {
		rtw_init_sema(&scan_result_handler_ptr.scan_semaphore, 1);
	}

	scan_result_handler_ptr.scan_start_time = rtw_get_current_time();
	/* Initialise the semaphore that will prevent simultaneous access - cannot be a mutex, since
	* we don't want to allow the same thread to start a new scan */
	result = (rtw_bool_t)rtw_down_timeout_sema(&scan_result_handler_ptr.scan_semaphore, SCAN_LONGEST_WAIT_TIME);
	if (result != RTW_TRUE) {
		/* Return error result, but set the semaphore to work the next time */
		rtw_up_sema(&scan_result_handler_ptr.scan_semaphore);
		return RTW_TIMEOUT;
	}
#else
	if (scan_result_handler_ptr.scan_running) {
		int count = 100;
		while (scan_result_handler_ptr.scan_running && count > 0) {
			rtw_msleep_os(20);
			count --;
		}
		if (count == 0) {
			RTW_API_INFO("\n\r[%d]WiFi: Scan is running. Wait 2s timeout.", rtw_get_current_time());
			return RTW_TIMEOUT;
		}
	}
	scan_result_handler_ptr.scan_start_time = rtw_get_current_time();
	scan_result_handler_ptr.scan_running = 1;
#endif

	scan_result_handler_ptr.gscan_result_handler = results_handler;

	scan_result_handler_ptr.max_ap_size = max_ap_size;
	scan_result_handler_ptr.ap_details = (rtw_scan_result_t *)rtw_zmalloc(max_ap_size * sizeof(rtw_scan_result_t));
	if (scan_result_handler_ptr.ap_details == NULL) {
		goto err_exit;
	}
	rtw_memset(scan_result_handler_ptr.ap_details, 0, max_ap_size * sizeof(rtw_scan_result_t));

	scan_result_handler_ptr.pap_details = (rtw_scan_result_t **)rtw_zmalloc(max_ap_size * sizeof(rtw_scan_result_t *));
	if (scan_result_handler_ptr.pap_details == NULL) {
		goto error2_with_result_ptr;
	}
	rtw_memset(scan_result_handler_ptr.pap_details, 0, max_ap_size * sizeof(rtw_scan_result_t *));

	scan_result_handler_ptr.scan_cnt = 0;

	scan_result_handler_ptr.scan_complete = RTW_FALSE;
	scan_result_handler_ptr.user_data = user_data;

	if (wifi_scan((rtw_scan_type_t)(RTW_SCAN_COMMAMD << 4 | RTW_SCAN_TYPE_ACTIVE), RTW_BSS_TYPE_ANY, NULL) != RTW_SUCCESS) {
		goto error1_with_result_ptr;
	}

	return RTW_SUCCESS;

error1_with_result_ptr:
	rtw_free((u8 *)scan_result_handler_ptr.pap_details);
	scan_result_handler_ptr.pap_details = NULL;

error2_with_result_ptr:
	rtw_free((u8 *)scan_result_handler_ptr.ap_details);
	scan_result_handler_ptr.ap_details = NULL;

err_exit:
	rtw_memset((void *)&scan_result_handler_ptr, 0, sizeof(scan_result_handler_ptr));
	return RTW_ERROR;
}

/*
 * SCAN_DONE_INTERVAL is the interval between each channel scan done,
 * to make AP mode can send beacon during this interval.
 * It is to fix client disconnection when doing wifi scan in AP/concurrent mode.
 * User can fine tune SCAN_DONE_INTERVAL value.
 */
#define SCAN_DONE_INTERVAL 100 //100ms

/*
 * Noted : the scan channel list needs to be modified depending on user's channel plan.
 */
#define SCAN_CHANNEL_NUM 13 //2.4GHz
u8 scan_channel_list[SCAN_CHANNEL_NUM] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13};

int wifi_scan_networks_mcc(rtw_scan_result_handler_t results_handler, void *user_data)
{
	unsigned int max_ap_size = 64;
	u8 channel_index;
	u8 pscan_config;
	int ret;

	/* lock 2s to forbid suspend under scan */
	rtw_wakelock_timeout(2 * 1000);

	for (channel_index = 0; channel_index < SCAN_CHANNEL_NUM; channel_index++) {
#if SCAN_USE_SEMAPHORE
		rtw_bool_t result;
		if (NULL == scan_result_handler_ptr.scan_semaphore) {
			rtw_init_sema(&scan_result_handler_ptr.scan_semaphore, 1);
		}

		scan_result_handler_ptr.scan_start_time = rtw_get_current_time();
		/* Initialise the semaphore that will prevent simultaneous access - cannot be a mutex, since
		* we don't want to allow the same thread to start a new scan */
		result = (rtw_bool_t)rtw_down_timeout_sema(&scan_result_handler_ptr.scan_semaphore, SCAN_LONGEST_WAIT_TIME);
		if (result != RTW_TRUE) {
			/* Return error result, but set the semaphore to work the next time */
			rtw_up_sema(&scan_result_handler_ptr.scan_semaphore);
			return RTW_TIMEOUT;
		}
#else
		if (scan_result_handler_ptr.scan_running) {
			int count = 200;
			while (scan_result_handler_ptr.scan_running && count > 0) {
				rtw_msleep_os(5);
				count --;
			}
			if (count == 0) {
				printf("\n\r[%d]WiFi: Scan is running. Wait 1s timeout.", rtw_get_current_time());
				return RTW_TIMEOUT;
			}
		}

		if (channel_index != 0) {
			vTaskDelay(SCAN_DONE_INTERVAL);
		}

		scan_result_handler_ptr.scan_running = 1;
		scan_result_handler_ptr.scan_start_time = rtw_get_current_time();
#endif
		if (channel_index == 0) {
			scan_result_handler_ptr.gscan_result_handler = results_handler;

			scan_result_handler_ptr.max_ap_size = max_ap_size;
			scan_result_handler_ptr.ap_details = (rtw_scan_result_t *)rtw_zmalloc(max_ap_size * sizeof(rtw_scan_result_t));
			if (scan_result_handler_ptr.ap_details == NULL) {
				goto err_exit;
			}
			rtw_memset(scan_result_handler_ptr.ap_details, 0, max_ap_size * sizeof(rtw_scan_result_t));

			scan_result_handler_ptr.pap_details = (rtw_scan_result_t **)rtw_zmalloc(max_ap_size * sizeof(rtw_scan_result_t *));
			if (scan_result_handler_ptr.pap_details == NULL) {
				goto error2_with_result_ptr;
			}
			rtw_memset(scan_result_handler_ptr.pap_details, 0, max_ap_size * sizeof(rtw_scan_result_t *));

			scan_result_handler_ptr.scan_cnt = 0;

			scan_result_handler_ptr.scan_complete = RTW_FALSE;
			scan_result_handler_ptr.user_data = user_data;
			wifi_reg_event_handler(WIFI_EVENT_SCAN_RESULT_REPORT, wifi_scan_each_report_hdl, NULL);
			wifi_reg_event_handler(WIFI_EVENT_SCAN_DONE, wifi_scan_done_hdl_mcc, NULL);
		}
		if (channel_index == SCAN_CHANNEL_NUM - 1) {
			wifi_unreg_event_handler(WIFI_EVENT_SCAN_DONE, wifi_scan_done_hdl_mcc);
			wifi_reg_event_handler(WIFI_EVENT_SCAN_DONE, wifi_scan_done_hdl, NULL);
		}

		pscan_config = PSCAN_ENABLE;
		//set partial scan for entering to listen beacon quickly
		ret = wifi_set_pscan_chan(&scan_channel_list[channel_index], &pscan_config, 1);
		if (ret < 0) {
#if SCAN_USE_SEMAPHORE
			rtw_up_sema(&scan_result_handler_ptr.scan_semaphore);
#else
			scan_result_handler_ptr.scan_running = 0;
#endif
			if (channel_index == SCAN_CHANNEL_NUM - 1) {
				wifi_scan_done_hdl(NULL, 0, 0, NULL);
			}
			continue;
		}

		if (wext_set_scan(WLAN0_NAME, NULL, 0, (RTW_SCAN_COMMAMD << 4 | RTW_SCAN_TYPE_ACTIVE | (RTW_BSS_TYPE_ANY << 8))) != RTW_SUCCESS) {
			goto error1_with_result_ptr;
		}
	}

	return RTW_SUCCESS;

error1_with_result_ptr:
	wifi_unreg_event_handler(WIFI_EVENT_SCAN_DONE, wifi_scan_done_hdl_mcc);
	wifi_unreg_event_handler(WIFI_EVENT_SCAN_RESULT_REPORT, wifi_scan_each_report_hdl);
	wifi_unreg_event_handler(WIFI_EVENT_SCAN_DONE, wifi_scan_done_hdl);
	rtw_free((u8 *)scan_result_handler_ptr.pap_details);
	scan_result_handler_ptr.pap_details = NULL;

error2_with_result_ptr:
	rtw_free((u8 *)scan_result_handler_ptr.ap_details);
	scan_result_handler_ptr.ap_details = NULL;

err_exit:
	rtw_memset((void *)&scan_result_handler_ptr, 0, sizeof(scan_result_handler_ptr));
	return RTW_ERROR;
}

//----------------------------------------------------------------------------//
int wifi_set_pscan_chan(__u8 *channel_list, __u8 *pscan_config, __u8 length)
{
	if (channel_list) {
		return wext_set_pscan_channel(WLAN0_NAME, channel_list, pscan_config, length);
	} else {
		return -1;
	}
}

/*
 * @brief get WIFI band type
 *@retval  the support band type.
 * 	WL_BAND_2_4G: only support 2.4G
 *	WL_BAND_5G: only support 5G
 *  WL_BAND_2_4G_5G_BOTH: support both 2.4G and 5G
 */
WL_BAND_TYPE wifi_get_band_type(void)
{
	u8 ret;

	ret = rltk_get_band_type();

	if (ret == 0) {
		return WL_BAND_2_4G;
	} else if (ret == 1) {
		return WL_BAND_5G;
	} else {
		return WL_BAND_2_4G_5G_BOTH;
	}
}

//----------------------------------------------------------------------------//
int wifi_get_setting(const char *ifname, rtw_wifi_setting_t *pSetting)
{
	int ret = 0;
	int mode = 0;
	unsigned short security = 0;
	unsigned int auth_type = 0;

	memset(pSetting, 0, sizeof(rtw_wifi_setting_t));
	if (wext_get_mode(ifname, &mode) < 0) {
		ret = -1;
	}

	switch (mode) {
	case IW_MODE_MASTER:
		pSetting->mode = RTW_MODE_AP;
		break;
	case IW_MODE_INFRA:
	default:
		pSetting->mode = RTW_MODE_STA;
		break;
		//default:
		//RTW_API_INFO("\r\n%s(): Unknown mode %d\n", __func__, mode);
		//break;
	}

	if (wext_get_ssid(ifname, pSetting->ssid) < 0) {
		ret = -1;
	}
	if (wext_get_channel(ifname, &pSetting->channel) < 0) {
		ret = -1;
	}
	if (wext_get_enc_ext(ifname, &security, &pSetting->key_idx, pSetting->password) < 0) {
		ret = -1;
	}
	if (wext_get_auth_type(ifname, &auth_type) < 0) {
		ret = -1;
	}

	switch (security) {
	case IW_ENCODE_ALG_NONE:
		pSetting->security_type = RTW_SECURITY_OPEN;
		break;
	case IW_ENCODE_ALG_WEP:
		pSetting->security_type = RTW_SECURITY_WEP_PSK;
		break;
	case IW_ENCODE_ALG_TKIP:
		if (auth_type == WPA_SECURITY) {
			pSetting->security_type = RTW_SECURITY_WPA_TKIP_PSK;
		} else if (auth_type == WPA2_SECURITY) {
			pSetting->security_type = RTW_SECURITY_WPA2_TKIP_PSK;
		}
		break;
	case IW_ENCODE_ALG_CCMP:
		if (auth_type == WPA_SECURITY) {
			pSetting->security_type = RTW_SECURITY_WPA_AES_PSK;
		} else if (auth_type == WPA2_SECURITY) {
			pSetting->security_type = RTW_SECURITY_WPA2_AES_PSK;
		} else if (auth_type == WPA3_SECURITY) {
			pSetting->security_type = RTW_SECURITY_WPA3_AES_PSK;
		}
		break;
	default:
		break;
	}

	if (security == IW_ENCODE_ALG_TKIP || security == IW_ENCODE_ALG_CCMP)
		if (wext_get_passphrase(ifname, pSetting->password) < 0) {
			ret = -1;
		}

	return ret;
}
//----------------------------------------------------------------------------//
int wifi_show_setting(const char *ifname, rtw_wifi_setting_t *pSetting)
{
	int ret = 0;
#ifndef CONFIG_INIC_NO_FLASH

	RTW_API_INFO("\n\r\nWIFI  %s Setting:", ifname);
	RTW_API_INFO("\n\r==============================");

	switch (pSetting->mode) {
	case RTW_MODE_AP:
#if (defined(CONFIG_EXAMPLE_UART_ATCMD) && CONFIG_EXAMPLE_UART_ATCMD) || (defined(CONFIG_EXAMPLE_SPI_ATCMD) && CONFIG_EXAMPLE_SPI_ATCMD)
		at_printf("\r\nAP,");
#endif
		RTW_API_INFO("\n\r      MODE => AP");
		break;
	case RTW_MODE_STA:
#if (defined(CONFIG_EXAMPLE_UART_ATCMD) && CONFIG_EXAMPLE_UART_ATCMD) || (defined(CONFIG_EXAMPLE_SPI_ATCMD) && CONFIG_EXAMPLE_SPI_ATCMD)
		at_printf("\r\nSTA,");
#endif
		RTW_API_INFO("\n\r      MODE => STATION");
		break;
	default:
#if (defined(CONFIG_EXAMPLE_UART_ATCMD) && CONFIG_EXAMPLE_UART_ATCMD) || (defined(CONFIG_EXAMPLE_SPI_ATCMD) && CONFIG_EXAMPLE_SPI_ATCMD)
		at_printf("\r\nUNKNOWN,");
#endif
		RTW_API_INFO("\n\r      MODE => UNKNOWN");
	}
#if (defined(CONFIG_EXAMPLE_UART_ATCMD) && CONFIG_EXAMPLE_UART_ATCMD) || (defined(CONFIG_EXAMPLE_SPI_ATCMD) && CONFIG_EXAMPLE_SPI_ATCMD)
	at_printf("%s,%d,", pSetting->ssid, pSetting->channel);
#endif
	RTW_API_INFO("\n\r      SSID => %s", pSetting->ssid);
	RTW_API_INFO("\n\r   CHANNEL => %d", pSetting->channel);

	switch (pSetting->security_type) {
	case RTW_SECURITY_OPEN:
#if (defined(CONFIG_EXAMPLE_UART_ATCMD) && CONFIG_EXAMPLE_UART_ATCMD) || (defined(CONFIG_EXAMPLE_SPI_ATCMD) && CONFIG_EXAMPLE_SPI_ATCMD)
		at_printf("OPEN,");
#endif
		RTW_API_INFO("\n\r  SECURITY => OPEN");
		break;
	case RTW_SECURITY_WEP_PSK:
#if (defined(CONFIG_EXAMPLE_UART_ATCMD) && CONFIG_EXAMPLE_UART_ATCMD) || (defined(CONFIG_EXAMPLE_SPI_ATCMD) && CONFIG_EXAMPLE_SPI_ATCMD)
		at_printf("WEP,%d,", pSetting->key_idx);
#endif
		RTW_API_INFO("\n\r  SECURITY => WEP");
		RTW_API_INFO("\n\r KEY INDEX => %d", pSetting->key_idx);
		break;
	case RTW_SECURITY_WPA_TKIP_PSK:
#if (defined(CONFIG_EXAMPLE_UART_ATCMD) && CONFIG_EXAMPLE_UART_ATCMD) || (defined(CONFIG_EXAMPLE_SPI_ATCMD) && CONFIG_EXAMPLE_SPI_ATCMD)
		at_printf("WPA TKIP,");
#endif
		RTW_API_INFO("\n\r  SECURITY => WPA TKIP");
		break;
	case RTW_SECURITY_WPA2_TKIP_PSK:
#if (defined(CONFIG_EXAMPLE_UART_ATCMD) && CONFIG_EXAMPLE_UART_ATCMD) || (defined(CONFIG_EXAMPLE_SPI_ATCMD) && CONFIG_EXAMPLE_SPI_ATCMD)
		at_printf("WPA2 TKIP,");
#endif
		RTW_API_INFO("\n\r  SECURITY => WPA2 TKIP");
		break;
	case RTW_SECURITY_WPA_AES_PSK:
#if (defined(CONFIG_EXAMPLE_UART_ATCMD) && CONFIG_EXAMPLE_UART_ATCMD) || (defined(CONFIG_EXAMPLE_SPI_ATCMD) && CONFIG_EXAMPLE_SPI_ATCMD)
		at_printf("WPA AES,");
#endif
		RTW_API_INFO("\n\r  SECURITY => WPA AES");
		break;
	case RTW_SECURITY_WPA2_AES_PSK:
#if (defined(CONFIG_EXAMPLE_UART_ATCMD) && CONFIG_EXAMPLE_UART_ATCMD) || (defined(CONFIG_EXAMPLE_SPI_ATCMD) && CONFIG_EXAMPLE_SPI_ATCMD)
		at_printf("WPA2 AES,");
#endif
		RTW_API_INFO("\n\r  SECURITY => WPA2 AES");
		break;
	case RTW_SECURITY_WPA3_AES_PSK:
#if (defined(CONFIG_EXAMPLE_UART_ATCMD) && CONFIG_EXAMPLE_UART_ATCMD) || (defined(CONFIG_EXAMPLE_SPI_ATCMD) && CONFIG_EXAMPLE_SPI_ATCMD)
		at_printf("WPA3 AES,");
#endif
		RTW_API_INFO("\n\r  SECURITY => WPA3 AES");
		break;
	default:
#if (defined(CONFIG_EXAMPLE_UART_ATCMD) && CONFIG_EXAMPLE_UART_ATCMD) || (defined(CONFIG_EXAMPLE_SPI_ATCMD) && CONFIG_EXAMPLE_SPI_ATCMD)
		at_printf("UNKNOWN,");
#endif
		RTW_API_INFO("\n\r  SECURITY => UNKNOWN");
	}

#if (defined(CONFIG_EXAMPLE_UART_ATCMD) && CONFIG_EXAMPLE_UART_ATCMD) || (defined(CONFIG_EXAMPLE_SPI_ATCMD) && CONFIG_EXAMPLE_SPI_ATCMD)
	at_printf("%s,", pSetting->password);
#endif
	RTW_API_INFO("\n\r  PASSWORD => %s", pSetting->password);
	RTW_API_INFO("\n\r");
#endif
	return ret;
}

//----------------------------------------------------------------------------//
int wifi_set_network_mode(rtw_network_mode_t mode)
{
	if ((mode == RTW_NETWORK_B) || (mode == RTW_NETWORK_BG) || (mode == RTW_NETWORK_BGN)) {
		return rltk_wlan_wireless_mode((unsigned char) mode);
	}

	return -1;
}

int wifi_get_network_mode(rtw_network_mode_t *pmode)
{
	if (pmode != NULL) {
		return rltk_wlan_get_wireless_mode((unsigned char *) pmode);
	}

	return -1;
}

int wifi_get_cur_network_mode(void)
{
	return rltk_wlan_get_cur_wireless_mode();
}

int wifi_set_wps_phase(unsigned char is_trigger_wps)
{
	return rltk_wlan_set_wps_phase(is_trigger_wps);
}

//----------------------------------------------------------------------------//
int wifi_set_promisc(rtw_rcr_level_t enabled, void (*callback)(unsigned char *, unsigned int, void *), unsigned char len_used)
{
	return promisc_set(enabled, callback, len_used);
}

void wifi_enter_promisc_mode()
{
#ifdef CONFIG_PROMISC
	int mode = 0;
	unsigned char ssid[33];

	if (!rltk_wlan_running(WLAN0_IDX)) {
		wifi_on(RTW_MODE_PROMISC);
		return;
	}

	if (wifi_mode == RTW_MODE_STA_AP) {
		wifi_off();
		rtw_msleep_os(20);
		wifi_on(RTW_MODE_PROMISC);
	} else {
		wext_get_mode(WLAN0_NAME, &mode);

		switch (mode) {
		case IW_MODE_MASTER:    //In AP mode
#if defined(CONFIG_PLATFORM_8710C) && (defined(CONFIG_BT) && CONFIG_BT)
			wifi_set_mode(RTW_MODE_PROMISC);
#else
			//rltk_wlan_deinit();
			wifi_off();//modified by Chris Yang for iNIC
			rtw_msleep_os(20);
			//rltk_wlan_init(0, RTW_MODE_PROMISC);
			//rltk_wlan_start(0);
			wifi_on(RTW_MODE_PROMISC);
#endif
			break;
		case IW_MODE_INFRA:		//In STA mode
			if (wext_get_ssid(WLAN0_NAME, ssid) > 0) {
				wifi_disconnect();
			}
		}
	}
#endif
}

int wifi_restart_ap(
	unsigned char 		*ssid,
	rtw_security_t		security_type,
	unsigned char 		*password,
	int 				ssid_len,
	int 				password_len,
	int					channel)
{
#if defined (CONFIG_AP_MODE) && defined (CONFIG_NATIVEAP_MLME)
	unsigned char idx = 0;
#if CONFIG_LWIP_LAYER
#if defined(CONFIG_MBED_ENABLED) || defined(CONFIG_PLATFOMR_CUSTOMER_RTOS)
	//TODO
#else
	ip_addr_t ipaddr;
	ip_addr_t netmask;
	ip_addr_t gw;
	struct netif *pnetif = &xnetif[0];
#endif
#endif
#ifdef  CONFIG_CONCURRENT_MODE
	rtw_wifi_setting_t setting;
	int sta_linked = 0;
#endif

	if (rltk_wlan_running(WLAN1_IDX)) {
		idx = 1;
	}

	// stop dhcp server
#if CONFIG_LWIP_LAYER
#if defined(CONFIG_MBED_ENABLED) || defined(CONFIG_PLATFOMR_CUSTOMER_RTOS)
	//TODO
#else
	dhcps_deinit();
#endif
#endif

#ifdef  CONFIG_CONCURRENT_MODE
	if (idx > 0) {
		sta_linked = wifi_get_setting(WLAN0_NAME, &setting);
		wifi_off();
		rtw_msleep_os(20);
		wifi_on(RTW_MODE_STA_AP);
	} else
#endif
	{
#if CONFIG_LWIP_LAYER
#if defined(CONFIG_MBED_ENABLED) || defined(CONFIG_PLATFOMR_CUSTOMER_RTOS)
		//TODO
#else
#if LWIP_VERSION_MAJOR >= 2
		IP4_ADDR(ip_2_ip4(&ipaddr), GW_ADDR0, GW_ADDR1, GW_ADDR2, GW_ADDR3);
		IP4_ADDR(ip_2_ip4(&netmask), NETMASK_ADDR0, NETMASK_ADDR1, NETMASK_ADDR2, NETMASK_ADDR3);
		IP4_ADDR(ip_2_ip4(&gw), GW_ADDR0, GW_ADDR1, GW_ADDR2, GW_ADDR3);
		netif_set_addr(pnetif, ip_2_ip4(&ipaddr), ip_2_ip4(&netmask), ip_2_ip4(&gw));
#else
		IP4_ADDR(&ipaddr, GW_ADDR0, GW_ADDR1, GW_ADDR2, GW_ADDR3);
		IP4_ADDR(&netmask, NETMASK_ADDR0, NETMASK_ADDR1, NETMASK_ADDR2, NETMASK_ADDR3);
		IP4_ADDR(&gw, GW_ADDR0, GW_ADDR1, GW_ADDR2, GW_ADDR3);
		netif_set_addr(pnetif, &ipaddr, &netmask, &gw);
#endif
#endif
#endif
		wifi_off();
		rtw_msleep_os(20);
		wifi_on(RTW_MODE_AP);
	}
	// start ap
	if (wifi_start_ap((char *)ssid, security_type, (char *)password, ssid_len, password_len, channel) < 0) {
		RTW_API_INFO("\n\rERROR: Operation failed!");
		return -1;
	}

#if (INCLUDE_uxTaskGetStackHighWaterMark == 1)
	RTW_API_INFO("\r\nWebServer Thread: High Water Mark is %ld\n", uxTaskGetStackHighWaterMark(NULL));
#endif
#ifdef  CONFIG_CONCURRENT_MODE
	// connect to ap if wlan0 was linked with ap
	if (idx > 0 && sta_linked == 0) {
		volatile int ret = 0;
		(void) ret;
		RTW_API_INFO("\r\nAP: ssid=%s", (char *)setting.ssid);
		RTW_API_INFO("\r\nAP: security_type=%d", setting.security_type);
		RTW_API_INFO("\r\nAP: password=%s", (char *)setting.password);
		RTW_API_INFO("\r\nAP: key_idx =%d\n", setting.key_idx);
		ret = wifi_connect((char *)setting.ssid,
						   setting.security_type,
						   (char *)setting.password,
						   strlen((char *)setting.ssid),
						   strlen((char *)setting.password),
						   setting.key_idx,
						   NULL);
#if defined(CONFIG_DHCP_CLIENT) && CONFIG_DHCP_CLIENT
		if (ret == RTW_SUCCESS) {
			/* Start DHCPClient */
			LwIP_DHCP(0, DHCP_START);
#if LWIP_VERSION_MAJOR >= 2 && LWIP_VERSION_MINOR >= 1
#if LWIP_IPV6 && LWIP_IPV6_DHCP6
			LwIP_DHCP6(0, DHCP6_START);
#endif
#endif
		}
#endif
	}
#endif
#if defined(CONFIG_MBED_ENABLED)
	osThreadId_t id = osThreadGetId();
	RTW_API_INFO("\r\nWebServer Thread: High Water Mark is %ld\n", osThreadGetStackSpace(id));
#else
#if (INCLUDE_uxTaskGetStackHighWaterMark == 1)
	RTW_API_INFO("\r\nWebServer Thread: High Water Mark is %ld\n", uxTaskGetStackHighWaterMark(NULL));
#endif
#endif
#if CONFIG_LWIP_LAYER
#if defined(CONFIG_MBED_ENABLED) || defined(CONFIG_PLATFOMR_CUSTOMER_RTOS)
	//TODO
#else
	// start dhcp server
	dhcps_init(&xnetif[idx]);
#endif
#endif
#endif
	return 0;
}

#if defined(CONFIG_AUTO_RECONNECT) && CONFIG_AUTO_RECONNECT
extern void (*p_wlan_autoreconnect_hdl)(rtw_security_t, char *, int, char *, int, int);

struct wifi_autoreconnect_param {
	rtw_security_t security_type;
	char *ssid;
	int ssid_len;
	char *password;
	int password_len;
	int key_id;
};

#if CONFIG_ENABLE_WPS
char wps_profile_ssid[33] = {'\0'};
char wps_profile_password[65] = {'\0'};
#endif

#if WIFI_LOGO_CERTIFICATION_CONFIG
extern u8 use_static_ip;
extern struct ip_addr g_ipaddr;
extern struct ip_addr g_netmask;
extern struct ip_addr g_gw;
#endif

#if defined(CONFIG_MBED_ENABLED) || defined(CONFIG_PLATFOMR_CUSTOMER_RTOS)
void wifi_autoreconnect_hdl(rtw_security_t security_type,
							char *ssid, int ssid_len,
							char *password, int password_len,
							int key_id)
{
	RTW_API_INFO("\n\r%s Not Implemented Yet!\n", __func__);
}
#else
static void wifi_autoreconnect_thread(void *param)
{
	int ret = RTW_ERROR;
	struct wifi_autoreconnect_param *reconnect_param = (struct wifi_autoreconnect_param *) param;
	RTW_API_INFO("\n\rauto reconnect ...\n");
	char empty_bssid[6] = {0}, assoc_by_bssid = 0;
	extern unsigned char *rltk_wlan_get_saved_bssid(void);
	unsigned char *saved_bssid = rltk_wlan_get_saved_bssid();

	if (memcmp(saved_bssid, empty_bssid, ETH_ALEN)) {
		assoc_by_bssid = 1;
	}

#if defined(CONFIG_SAE_SUPPORT) && (CONFIG_ENABLE_WPS==1)
	unsigned char is_wpa3_disable = 0;
	if ((strncmp(wps_profile_ssid, reconnect_param->ssid, reconnect_param->ssid_len) == 0) &&
		(strncmp(wps_profile_password, reconnect_param->password, reconnect_param->password_len) == 0) &&
		(wext_get_support_wpa3() == 1)) {
		wext_set_support_wpa3(DISABLE);
		is_wpa3_disable = 1;
	}
#endif

	if (assoc_by_bssid) {
		ret = wifi_connect_bssid(saved_bssid, reconnect_param->ssid, reconnect_param->security_type,
								 reconnect_param->password, ETH_ALEN, reconnect_param->ssid_len, reconnect_param->password_len, reconnect_param->key_id, NULL);
	} else {
		ret = wifi_connect(reconnect_param->ssid, reconnect_param->security_type, reconnect_param->password,
						   reconnect_param->ssid_len, reconnect_param->password_len, reconnect_param->key_id, NULL);
	}

#if defined(CONFIG_SAE_SUPPORT) && (CONFIG_ENABLE_WPS==1)
	if (is_wpa3_disable) {
		wext_set_support_wpa3(ENABLE);
	}
#endif

#if CONFIG_LWIP_LAYER
	if (ret == RTW_SUCCESS) {
#if ATCMD_VER == ATVER_2
		if (dhcp_mode_sta == 2) {
			struct netif *pnetif = &xnetif[0];
			LwIP_UseStaticIP(pnetif);
			dhcps_init(pnetif);
		} else
#endif
		{
#if WIFI_LOGO_CERTIFICATION_CONFIG
			if (use_static_ip) {
				netif_set_addr(&xnetif[0], ip_2_ip4(&g_ipaddr), ip_2_ip4(&g_netmask), ip_2_ip4(&g_gw));
			} else
#endif
			{
				LwIP_DHCP(0, DHCP_START);
#if LWIP_VERSION_MAJOR >= 2 && LWIP_VERSION_MINOR >= 1
#if LWIP_IPV6 && LWIP_IPV6_DHCP6
				LwIP_DHCP6(0, DHCP6_START);
#endif
#endif
#if LWIP_AUTOIP
				uint8_t *ip = LwIP_GetIP(&xnetif[0]);
				if ((ip[0] == 0) && (ip[1] == 0) && (ip[2] == 0) && (ip[3] == 0)) {
					RTW_API_INFO("\n\nIPv4 AUTOIP ...");
					LwIP_AUTOIP(&xnetif[0]);
				}
#endif
			}
		}
	}
#endif //#if CONFIG_LWIP_LAYER
	param_indicator = NULL;
	rtw_delete_task(&wifi_autoreconnect_task);
}

void wifi_autoreconnect_hdl(rtw_security_t security_type,
							char *ssid, int ssid_len,
							char *password, int password_len,
							int key_id)
{
	static struct wifi_autoreconnect_param param;
	param_indicator = &param;
	param.security_type = security_type;
	param.ssid = ssid;
	param.ssid_len = ssid_len;
	param.password = password;
	param.password_len = password_len;
	param.key_id = key_id;

	if (wifi_autoreconnect_task.task != NULL) {
		dhcp_stop(&xnetif[0]);
		u32 start_tick = rtw_get_current_time();
		while (1) {
			rtw_msleep_os(2);
			u32 passing_tick = rtw_get_current_time() - start_tick;
			if (rtw_systime_to_sec(passing_tick) >= 2) {
				RTW_API_INFO("\r\n Create wifi_autoreconnect_task timeout \r\n");
				return;
			}

			if (wifi_autoreconnect_task.task == NULL) {
				break;
			}
		}
	}

#ifdef PLATFORM_OHOS
	rtw_create_task(&wifi_autoreconnect_task, (const char *)"wifi_autoreconnect", 512, 1, wifi_autoreconnect_thread, &param);
#else
	rtw_create_task(&wifi_autoreconnect_task, (const char *)"wifi_autoreconnect", 512, tskIDLE_PRIORITY + 1, wifi_autoreconnect_thread, &param);
#endif
}
#endif

int wifi_config_autoreconnect(__u8 mode, __u8 retry_times, __u16 timeout)
{
	if (mode == RTW_AUTORECONNECT_DISABLE) {
		p_wlan_autoreconnect_hdl = NULL;
	} else {
		p_wlan_autoreconnect_hdl = wifi_autoreconnect_hdl;
	}
	return rltk_wlan_set_autoreconnect(WLAN0_NAME, mode, retry_times, timeout);
}

int wifi_set_autoreconnect(__u8 mode)
{
	p_wlan_autoreconnect_hdl = wifi_autoreconnect_hdl;
	return wifi_config_autoreconnect(mode, AUTO_RECONNECT_COUNT, AUTO_RECONNECT_INTERVAL);//default retry 8 times, timeout 5 seconds
}

int wifi_get_autoreconnect(__u8 *mode)
{
	return rltk_wlan_get_autoreconnect(WLAN0_NAME, mode);
}
#endif

#if defined( CONFIG_ENABLE_AP_POLLING_CLIENT_ALIVE )&&( CONFIG_ENABLE_AP_POLLING_CLIENT_ALIVE == 1 )
extern void (*p_ap_polling_sta_hdl)(void *);
extern void (*p_ap_polling_sta_int_hdl)(void *, u16, u32);
extern void ap_polling_sta_hdl(void *);
extern void ap_polling_sta_int_hdl(void *, u16, u32);

void  wifi_set_ap_polling_sta(__u8 enabled)
{
	if (_TRUE == enabled) {
		p_ap_polling_sta_hdl = ap_polling_sta_hdl;
		p_ap_polling_sta_int_hdl = ap_polling_sta_int_hdl;
	} else {
		p_ap_polling_sta_hdl = NULL;
		p_ap_polling_sta_int_hdl = NULL;
	}
	return ;
}
#endif

#ifdef CONFIG_CUSTOM_IE
/*
 * Example for custom ie
 *
 * u8 test_1[] = {221, 2, 2, 2};
 * u8 test_2[] = {221, 2, 1, 1};
 * rtw_custom_ie_t buf[2] = {{test_1, PROBE_REQ},
 *		 {test_2, PROBE_RSP | BEACON}};
 * u8 buf_test2[] = {221, 2, 1, 3} ;
 * rtw_custom_ie_t buf_update = {buf_test2, PROBE_REQ};
 *
 * add ie list
 * static void cmd_add_ie(int argc, char **argv)
 * {
 *	 wifi_add_custom_ie((void *)buf, 2);
 * }
 *
 * update current ie
 * static void cmd_update_ie(int argc, char **argv)
 * {
 *	 wifi_update_custom_ie(&buf_update, 2);
 * }
 *
 * delete all ie
 * static void cmd_del_ie(int argc, char **argv)
 * {
 *	 wifi_del_custom_ie();
 * }
 */

int wifi_add_custom_ie(void *cus_ie, int ie_num)
{
	return wext_add_custom_ie(WLAN0_NAME, cus_ie, ie_num);
}


int wifi_update_custom_ie(void *cus_ie, int ie_index)
{
	return wext_update_custom_ie(WLAN0_NAME, cus_ie, ie_index);
}

int wifi_del_custom_ie()
{
	return wext_del_custom_ie(WLAN0_NAME);
}

#endif

#ifdef CONFIG_PROMISC
extern void promisc_init_packet_filter(void);
extern int promisc_add_packet_filter(u8 filter_id, rtw_packet_filter_pattern_t *patt, rtw_packet_filter_rule_t rule);
extern int promisc_enable_packet_filter(u8 filter_id);
extern int promisc_disable_packet_filter(u8 filter_id);
extern int promisc_remove_packet_filter(u8 filter_id);
extern int promisc_filter_retransmit_pkt(u8 enable, u8 filter_interval_ms);
extern void promisc_filter_by_ap_and_phone_mac(u8 enable, void *ap_mac, void *phone_mac);
extern int promisc_ctrl_packet_rpt(u8 enable);

void wifi_init_packet_filter()
{
	promisc_init_packet_filter();
}

int wifi_add_packet_filter(unsigned char filter_id, rtw_packet_filter_pattern_t *patt, rtw_packet_filter_rule_t rule)
{
	return promisc_add_packet_filter(filter_id, patt, rule);
}

int wifi_enable_packet_filter(unsigned char filter_id)
{
	return promisc_enable_packet_filter(filter_id);
}

int wifi_disable_packet_filter(unsigned char filter_id)
{
	return promisc_disable_packet_filter(filter_id);
}

int wifi_remove_packet_filter(unsigned char filter_id)
{
	return promisc_remove_packet_filter(filter_id);
}

int wifi_retransmit_packet_filter(u8 enable, u8 filter_interval_ms)
{
	return promisc_filter_retransmit_pkt(enable, filter_interval_ms);
}

void wifi_filter_by_ap_and_phone_mac(u8 enable, void *ap_mac, void *phone_mac)
{
	promisc_filter_by_ap_and_phone_mac(enable, ap_mac, phone_mac);
}

int wifi_promisc_ctrl_packet_rpt(u8 enable)
{
	return promisc_ctrl_packet_rpt(enable);
}
#endif

#ifdef CONFIG_AP_MODE
extern int wext_enable_forwarding(const char *ifname);
extern int wext_disable_forwarding(const char *ifname);
int wifi_enable_forwarding(void)
{
	return wext_enable_forwarding(WLAN0_NAME);
}

int wifi_disable_forwarding(void)
{
	return wext_disable_forwarding(WLAN0_NAME);
}
#endif

/* API to set flag for concurrent mode wlan1 issue_deauth when channel switched by wlan0
 * usage: wifi_set_ch_deauth(0) -> wlan0 wifi_connect -> wifi_set_ch_deauth(1)
 */
#ifdef CONFIG_CONCURRENT_MODE
int wifi_set_ch_deauth(__u8 enable)
{
	return wext_set_ch_deauth(WLAN1_NAME, enable);
}
#endif

void wifi_set_indicate_mgnt(int enable)
{
	if (rltk_wlan_running(WLAN0_IDX)) {
		wext_set_indicate_mgnt(enable);
	} else {
		printf("\nWiFi Disabled: Cannot set indicate mgnt\n");
	}
	return;
}

void wifi_set_lowrssi_use_b(int enable, int rssi)
{
	wext_set_lowrssi_use_b(enable, rssi);
	return;
}

#ifdef CONFIG_ANTENNA_DIVERSITY
int wifi_get_antenna_info(unsigned char *antenna)
{
	int ret = 0;
	int ant;
	char buf[32] = {0};
	rtw_memset(buf, 0, sizeof(buf));
	rtw_memcpy(buf, "get_ant_info", 12);
	ret = wext_private_command_with_retval(WLAN0_NAME, buf, buf, 32);
	sscanf(buf, "%d", &ant); // 0: main, 1: aux
	*antenna = (unsigned char)ant;
	return ret;
}
#endif

#ifdef CONFIG_CONCURRENT_MODE
extern void wext_suspend_softap(const char *ifname);
extern void wext_suspend_softap_beacon(const char *ifname);
void wifi_suspend_softap(void)
{
	int client_number;
	struct {
		int count;
		rtw_mac_t mac_list[AP_STA_NUM];
	} client_info;
	memset(&client_info, 0, sizeof(client_info));
	client_info.count = AP_STA_NUM;
	wifi_get_associated_client_list(&client_info, sizeof(client_info));
	for (client_number = 0; client_number < client_info.count; client_number ++) {
		wext_del_station(WLAN1_NAME, client_info.mac_list[client_number].octet);
	}
	wext_suspend_softap(WLAN1_NAME);
}

void wifi_suspend_softap_beacon(void)
{
	int client_number;
	struct {
		int count;
		rtw_mac_t mac_list[AP_STA_NUM];
	} client_info;
	memset(&client_info, 0, sizeof(client_info));
	client_info.count = AP_STA_NUM;
	wifi_get_associated_client_list(&client_info, sizeof(client_info));
	for (client_number = 0; client_number < client_info.count; client_number ++) {
		wext_del_station(WLAN1_NAME, client_info.mac_list[client_number].octet);
	}
	wext_suspend_softap_beacon(WLAN1_NAME);
}
#endif

#ifdef CONFIG_SW_MAILBOX_EN
int mailbox_to_wifi(u8 *data, u8 len)
{
	return wext_mailbox_to_wifi(WLAN0_NAME, data, len);
}
#endif

#ifdef CONFIG_WOWLAN_TCP_KEEP_ALIVE
#define IP_HDR_LEN   20
#define TCP_HDR_LEN  20
#define ETH_HDR_LEN  14
#define ETH_ALEN     6
static uint32_t _checksum32(uint32_t start_value, uint8_t *data, size_t len)
{
	uint32_t checksum32 = start_value;
	uint16_t data16 = 0;
	int i;

	for (i = 0; i < (len / 2 * 2); i += 2) {
		data16 = (data[i] << 8) | data[i + 1];
		checksum32 += data16;
	}

	if (len % 2) {
		data16 = data[len - 1] << 8;
		checksum32 += data16;
	}

	return checksum32;
}

static uint16_t _checksum32to16(uint32_t checksum32)
{
	uint16_t checksum16 = 0;

	checksum32 = (checksum32 >> 16) + (checksum32 & 0x0000ffff);
	checksum32 = (checksum32 >> 16) + (checksum32 & 0x0000ffff);
	checksum16 = (uint16_t) ~(checksum32 & 0xffff);

	return checksum16;
}
#endif

int wifi_set_tcp_keep_alive_offload(int socket_fd, uint8_t *content, size_t len, uint32_t interval_ms)
{
	/* To avoid gcc warnings */
	(void) socket_fd;
	(void) content;
	(void) len;
	(void) interval_ms;

#ifdef CONFIG_WOWLAN_TCP_KEEP_ALIVE
	struct sockaddr_in peer_addr, sock_addr;
	socklen_t peer_addr_len = sizeof(peer_addr);
	socklen_t sock_addr_len = sizeof(sock_addr);
	getpeername(socket_fd, (struct sockaddr *) &peer_addr, &peer_addr_len);
	getsockname(socket_fd, (struct sockaddr *) &sock_addr, &sock_addr_len);
	uint8_t *peer_ip = (uint8_t *) &peer_addr.sin_addr;
	uint16_t peer_port = ntohs(peer_addr.sin_port);
	uint8_t *sock_ip = (uint8_t *) &sock_addr.sin_addr;
	uint16_t sock_port = ntohs(sock_addr.sin_port);

	if (!rltk_wlan_running(WLAN0_IDX)) {
		return -1;
	}

	// ip header
	uint8_t ip_header[IP_HDR_LEN] = {0x45, 0x00, /*len*/ 0x00, 0x00 /*len*/, /*id*/ 0x00, 0x00 /*id*/, 0x00, 0x00, 0xff, /*protocol*/ 0x00 /*protocol*/,
									 /*chksum*/ 0x00, 0x00 /*chksum*/, /*srcip*/ 0x00, 0x00, 0x00, 0x00 /*srcip*/, /*dstip*/ 0x00, 0x00, 0x00, 0x00 /*dstip*/
									};
	// len
	uint16_t ip_len = IP_HDR_LEN + TCP_HDR_LEN + len;
	ip_header[2] = (uint8_t)(ip_len >> 8);
	ip_header[3] = (uint8_t)(ip_len & 0xff);
	// id
	extern u16_t ip4_getipid(void);
	uint16_t ip_id = ip4_getipid();
	ip_header[4] = (uint8_t)(ip_id >> 8);
	ip_header[5] = (uint8_t)(ip_id & 0xff);
	// protocol
	ip_header[9] = 0x06;
	// src ip
	ip_header[12] = sock_ip[0];
	ip_header[13] = sock_ip[1];
	ip_header[14] = sock_ip[2];
	ip_header[15] = sock_ip[3];
	// dst ip
	ip_header[16] = peer_ip[0];
	ip_header[17] = peer_ip[1];
	ip_header[18] = peer_ip[2];
	ip_header[19] = peer_ip[3];
	// checksum
	uint32_t ip_checksum32 = 0;
	uint16_t ip_checksum16 = 0;
	ip_checksum32 = _checksum32(ip_checksum32, ip_header, sizeof(ip_header));
	ip_checksum16 = _checksum32to16(ip_checksum32);
	ip_header[10] = (uint8_t)(ip_checksum16 >> 8);
	ip_header[11] = (uint8_t)(ip_checksum16 & 0xff);

	// pseudo header
	uint8_t pseudo_header[12] = {/*srcip*/ 0x00, 0x00, 0x00, 0x00 /*srcip*/, /*dstip*/ 0x00, 0x00, 0x00, 0x00 /*dstip*/,
										   0x00, /*protocol*/ 0x00 /*protocol*/, /*l4len*/ 0x00, 0x00 /*l4len*/
								};
	// src ip
	pseudo_header[0] = sock_ip[0];
	pseudo_header[1] = sock_ip[1];
	pseudo_header[2] = sock_ip[2];
	pseudo_header[3] = sock_ip[3];
	// dst ip
	pseudo_header[4] = peer_ip[0];
	pseudo_header[5] = peer_ip[1];
	pseudo_header[6] = peer_ip[2];
	pseudo_header[7] = peer_ip[3];
	// protocol
	pseudo_header[9] = 0x06;
	// layer 4 len
	uint16_t l4_len = TCP_HDR_LEN + len;
	pseudo_header[10] = (uint8_t)(l4_len >> 8);
	pseudo_header[11] = (uint8_t)(l4_len & 0xff);

	// tcp header
	uint8_t tcp_header[TCP_HDR_LEN] = {/*srcport*/ 0x00, 0x00 /*srcport*/, /*dstport*/ 0x00, 0x00 /*dstport*/, /*seqno*/ 0x00, 0x00, 0x00, 0x00 /*seqno*/,
												   /*ackno*/ 0x00, 0x00, 0x00, 0x00 /*ackno*/, 0x50, 0x18, /*window*/ 0x00, 0x00 /*window*/, /*checksum*/ 0x00, 0x00 /*checksum*/, 0x00, 0x00
									  };
	// src port
	tcp_header[0] = (uint8_t)(sock_port >> 8);
	tcp_header[1] = (uint8_t)(sock_port & 0xff);
	// dst port
	tcp_header[2] = (uint8_t)(peer_port >> 8);
	tcp_header[3] = (uint8_t)(peer_port & 0xff);

	uint32_t seqno = 0;
	uint32_t ackno = 0;
	uint16_t wnd = 0;
	extern int lwip_gettcpstatus(int s, uint32_t *seqno, uint32_t *ackno, uint16_t *wnd);
	lwip_gettcpstatus(socket_fd, &seqno, &ackno, &wnd);
	// seqno
	tcp_header[4] = (uint8_t)(seqno >> 24);
	tcp_header[5] = (uint8_t)((seqno & 0x00ff0000) >> 16);
	tcp_header[6] = (uint8_t)((seqno & 0x0000ff00) >> 8);
	tcp_header[7] = (uint8_t)(seqno & 0x000000ff);
	// ackno
	tcp_header[8] = (uint8_t)(ackno >> 24);
	tcp_header[9] = (uint8_t)((ackno & 0x00ff0000) >> 16);
	tcp_header[10] = (uint8_t)((ackno & 0x0000ff00) >> 8);
	tcp_header[11] = (uint8_t)(ackno & 0x000000ff);
	// window
	tcp_header[14] = (uint8_t)(wnd >> 8);
	tcp_header[15] = (uint8_t)(wnd & 0xff);
	// checksum
	uint32_t tcp_checksum32 = 0;
	uint16_t tcp_checksum16 = 0;
	tcp_checksum32 = _checksum32(tcp_checksum32, pseudo_header, sizeof(pseudo_header));
	tcp_checksum32 = _checksum32(tcp_checksum32, tcp_header, sizeof(tcp_header));
	tcp_checksum32 = _checksum32(tcp_checksum32, content, len);
	tcp_checksum16 = _checksum32to16(tcp_checksum32);
	tcp_header[16] = (uint8_t)(tcp_checksum16 >> 8);
	tcp_header[17] = (uint8_t)(tcp_checksum16 & 0xff);

	// eth header
	uint8_t eth_header[ETH_HDR_LEN] = {/*dstaddr*/ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 /*dstaddr*/,
												   /*srcaddr*/ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 /*srcaddr*/, 0x08, 0x00
									  };
	struct eth_addr *dst_eth_ret = NULL;
	ip4_addr_t *dst_ip, *dst_ip_ret = NULL;
	dst_ip = (ip4_addr_t *) peer_ip;
	if (!ip4_addr_netcmp(dst_ip, netif_ip4_addr(&xnetif[0]), netif_ip4_netmask(&xnetif[0]))) {
		//outside local network
		dst_ip = (ip4_addr_t *) netif_ip4_gw(&xnetif[0]);
	}
	// dst addr
	if (etharp_find_addr(&xnetif[0], dst_ip, &dst_eth_ret, &dst_ip_ret) >= 0) {
		memcpy(eth_header, dst_eth_ret->addr, ETH_ALEN);
	}
	// src addr
	memcpy(eth_header + ETH_ALEN, LwIP_GetMAC(&xnetif[0]), ETH_ALEN);

	// eth frame without FCS
	uint32_t frame_len = sizeof(eth_header) + sizeof(ip_header) + sizeof(tcp_header) + len;
	uint8_t *eth_frame = (uint8_t *) malloc(frame_len);
	memcpy(eth_frame, eth_header, sizeof(eth_header));
	memcpy(eth_frame + sizeof(eth_header), ip_header, sizeof(ip_header));
	memcpy(eth_frame + sizeof(eth_header) + sizeof(ip_header), tcp_header, sizeof(tcp_header));
	memcpy(eth_frame + sizeof(eth_header) + sizeof(ip_header) + sizeof(tcp_header), content, len);

	//extern void rtw_set_keepalive_offload(uint8_t *eth_frame, uint32_t frame_len, uint32_t interval_ms);
	rtw_set_keepalive_offload(eth_frame, frame_len, interval_ms);

	free(eth_frame);

	return 0;
#else
	return -1;
#endif
}

//----------------------------------------------------------------------------//
#ifdef CONFIG_WOWLAN
int wifi_wlan_redl_fw(void)
{
	int ret = 0;

	ret = wext_wlan_redl_fw(WLAN0_NAME);

	return ret;
}

int wifi_wowlan_ctrl(int enable)
{
	int ret = 0;

	ret = wext_wowlan_ctrl(WLAN0_NAME, enable);

	return ret;
}

#ifdef CONFIG_WOWLAN_CUSTOM_PATTERN
int wifi_wowlan_set_pattern(wowlan_pattern_t pattern)
{
	int ret = 0;

	ret = wext_wowlan_set_pattern(WLAN0_NAME, pattern);

	return ret;
}
#endif
#endif
//----------------------------------------------------------------------------//
/**
  * @brief	App can enable or disable wifi radio directly through this API.
  * @param	new_status: the new state of the WIFI RF.
  *				This parameter can be: ON/OFF.
  * @retval	0: success
  * @note	wifi can't Tx/Rx packet after radio off, so you use this API carefully!!
  */
int wifi_rf_contrl(uint32_t new_status)
{
#ifdef CONFIG_APP_CTRL_RF_ONOFF
	rtw_rf_cmd(new_status);
#endif

	return 0;
}

/**
  * @brief	Get wifi TSF[63:0].
  * @param	port: wifi port 0/1.
  * @retval	TSF[63:0]
  */
u32 wifi_get_tsf_low(u32 port)
{
#ifdef CONFIG_APP_CTRL_RF_ONOFF
	return rtw_get_tsf(port);
#else
	return 0;
#endif
}

#if WIFI_LOGO_CERTIFICATION_CONFIG
#ifdef CONFIG_IEEE80211W
u32 wifi_set_pmf(unsigned char pmf_mode)
{
	int ret;
	ret = rltk_set_pmf(pmf_mode);
	return ret;
}
#endif
#endif

#ifdef CONFIG_MCC_STA_AP_MODE
extern int rltk_wlan_txrpt_statistic(const char *ifname, rtw_fw_txrpt_stats_t *txrpt_stats);
extern int rltk_wlan_ccx_txrpt_retry(rtw_fw_txrpt_retry_t *txrpt_retry);
extern void rltk_wlan_enable_ccx_txrpt(const char *ifname, int enable);
extern int rltk_wlan_set_mgnt_retry_lmt(unsigned char retry_limit);
extern int rltk_wlan_set_data_retry_lmt(unsigned char retry_limit);
extern int rltk_wlan_get_mgnt_retry_lmt(void);
extern int rltk_wlan_get_data_retry_lmt(void);
extern int rltk_wlan_set_aggregation(unsigned char option, rtw_ampdu_mode_t path);
extern int rltk_wlan_get_aggregation(rtw_ampdu_mode_t path);
extern int rltk_wlan_set_block_bc_mc_packet(unsigned char enable, unsigned char types);
extern int rltk_wlan_get_block_bc_mc_packet(unsigned char types);

int wifi_get_txrpt_statistic(const char *ifname, rtw_fw_txrpt_stats_t *txrpt_stats)
{
	return rltk_wlan_txrpt_statistic(ifname, txrpt_stats);
}

int wifi_get_ccx_txrpt_retry(rtw_fw_txrpt_retry_t *txrpt_retry)
{
	return rltk_wlan_ccx_txrpt_retry(txrpt_retry);
}

void wifi_enable_ccx_txrpt(const char *ifname, int enable)
{
	rltk_wlan_enable_ccx_txrpt(ifname, enable);
}

int wifi_set_mgnt_rt_lmt(unsigned char retry_limit)
{
	return rltk_wlan_set_mgnt_retry_lmt(retry_limit);
}

int wifi_set_data_rt_lmt(unsigned char retry_limit)
{
	return rltk_wlan_set_data_retry_lmt(retry_limit);
}

int wifi_get_mgnt_rt_lmt(void)
{
	return rltk_wlan_get_mgnt_retry_lmt();
}

int wifi_get_data_rt_lmt(void)
{
	return rltk_wlan_get_data_retry_lmt();
}

int wifi_set_aggregation(unsigned char option, rtw_ampdu_mode_t path)
{
	return rltk_wlan_set_aggregation(option, path);
}

int wifi_get_aggregation(rtw_ampdu_mode_t path)
{
	return rltk_wlan_get_aggregation(path);
}

int wifi_set_block_bc_mc_packet(unsigned char enable, unsigned char types)
{
	return rltk_wlan_set_block_bc_mc_packet(enable, types);
}

int wifi_get_block_bc_mc_packet(unsigned char types)
{
	return rltk_wlan_get_block_bc_mc_packet(types);
}
#endif

#endif	//#if CONFIG_WLAN
