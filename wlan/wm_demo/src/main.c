/*
 *  Copyright (C) 2008-2013, Marvell International Ltd.
 *  All Rights Reserved.
 */

/* WLAN Application using micro-AP and station
 *
 * Summary:
 *
 * This application showcases the end-to-end functionality of the SDK,
 * from configuring the device for the first time (provisioning),
 * to connection with the configured APs, to advertising mdns services,
 * to providing HTTP/HTML interfaces for interaction with the device,
 * to communicating with the cloud server.
 * This application also showcases the use of advanced features like
 * overlays, power management.
 *
 * The typical scenario in which you would use wm_demo, is to copy it
 * as your project and make modifications to the same.
 *
 * Description:
 *
 * The application is written using Application Framework that
 * simplifies development of WLAN networking applications.
 *
 * WLAN Initialization:
 *
 * When the application framework is started, it starts up the WLAN
 * sub-system and initializes the network stack. The app receives the event
 * when the WLAN subsystem has been started and initialized.
 *
 * If the device is not provisioned, Application framework sends an event
 * to the app to start a micro-AP network along with a DHCP server. This
 * will create a WLAN network and also creates a IP network interface
 * associated with that WLAN network. The DHCP server allows devices
 * to associate and connect to this network and establish an IP level
 * connection.
 *
 * When the network is initialized and ready, app will receive a
 * Micro-AP started event.
 *
 * Micro-AP Started:
 *
 * The micro-AP network is now up and ready.
 *
 * The app starts the web-server which will then listen to incoming http
 * connections (e.g, from a browser).
 *
 * By default, device can be accessed at URI http://192.168.10.1
 * Through the Web UI, user can provision the device to an Access Point.
 * Once the device is provisioned successfully to the configured Access
 * Point, app will receive a CONNECTED event.
 *
 * CONNECTED:
 *
 * Now the station interface of the device is up and app takes the
 * Micro-AP interface down. App enables MC200 + WiFi power save mode
 *
 * Cloud support:
 * By default, cloud support is not active. User can activate it by
 * executing following commands on the prompt of the device:
 * psm-set cloud enabled 1
 * psm-set cloud url "http://<ip-address>/cloud"
 * After the device reboot, cloud will get activated.
 *
 */
#include <wm_os.h>

#include <wm_os.h>
#include <app_framework.h>
#include <wmtime.h>
#include <partition.h>
#include <appln_cb.h>
#include <appln_dbg.h>
#include <cli.h>
#include <wmstdio.h>
#include <wm_net.h>
#include <mdns_helper.h>
#include <wps_helper.h>
#include <reset_prov_helper.h>
#include <power_mgr_helper.h>
#include <httpd.h>
#include <wmcloud.h>
#include <led_indicator.h>
#include <board.h>
#include <dhcp-server.h>
#include <wmtime.h>
#include <psm.h>
#include <ftfs.h>
#include <rfget.h>
#include <diagnostics.h>
#include <mdev_gpio.h>
#include <mdev_pinmux.h>

#include <healthmon.h>
#include "wm_demo_cloud.h"
#include "wm_demo_wps_cli.h"
#include <wm_demo_overlays.h>


/*-----------------------Global declarations----------------------*/
static char g_uap_ssid[IEEEtypes_SSID_SIZE + 1];
appln_config_t appln_cfg = {
	.passphrase = "marvellwm",
	.wps_pb_gpio = -1,
	.reset_prov_pb_gpio = -1
};

int ftfs_api_version = 100;
char *ftfs_part_name = "ftfs";

struct fs *fs;

static os_timer_t uap_down_timer;

#define UAP_DOWN_TIMEOUT (30 * 1000)

#define NETWORK_MOD_NAME	"network"
#define VAR_UAP_SSID		"uap_ssid"
#define VAR_PROV_KEY            "prov_key"
//#define APPCONFIG_PROV_EZCONNECT
#define APPCONFIG_MDNS_ENABLE 1

char PROV_EZCONNECT = 1;


os_semaphore_t button_sem;
/* Thread handle */
static os_thread_t app_thread_button;
static os_thread_t app_thread_http_listen;

/* Buffer to be used as stack */
static os_thread_stack_define(app_stack_button, 1024);
static os_thread_stack_define(app_stack_http_listen, 1024);

extern cloud_t c;

static struct json_str jstr;
static struct json_object obj;


/** Provisioning done timer call back function
 * Once the provisioning is done, we wait for provisioning client to send
 * AF_EVT_PROV_CLIENT_DONE which stops uap and dhcp server. But if any case
 * client doesn't send AF_EVT_PROV_CLIENT_DONE event, then we wait for
 * 60seconds(timer) to shutdown UAP.
 */
static void uap_down_timer_cb(os_timer_arg_t arg)
{
	if (is_uap_started()) {
		hp_mdns_deannounce(net_get_uap_handle());
		app_uap_stop();
	}
}

/* This function initializes the SSID with the PSM variable network.uap_ssid
 * If the variable is not found, the default value is used.
 * To change the ssid, please set the network.uap_ssid variable
 * from the console.
 */
void appln_init_ssid()
{
	if (psm_get_single(NETWORK_MOD_NAME, VAR_UAP_SSID, g_uap_ssid,
			sizeof(g_uap_ssid)) == WM_SUCCESS) {
		dbg("Using %s as the uAP SSID", g_uap_ssid);
		appln_cfg.ssid = g_uap_ssid;
		appln_cfg.hostname = g_uap_ssid;
	} else {
			uint8_t my_mac[6];

			memset(g_uap_ssid, 0, sizeof(g_uap_ssid));
			wlan_get_mac_address(my_mac);
			/* Provisioning SSID */
			snprintf(g_uap_ssid, sizeof(g_uap_ssid),
				 "wmdemo-%02X%02X", my_mac[4], my_mac[5]);
			dbg("Using %s as the uAP SSID", g_uap_ssid);
			appln_cfg.ssid = g_uap_ssid;
			appln_cfg.hostname = g_uap_ssid;
	}
}

#define KEY_LEN 16
uint8_t prov_key[KEY_LEN + 1]; /* One extra length to store \0" */

int wmdemo_get_prov_key(uint8_t *prov_key)
{

	if (psm_get_single(NETWORK_MOD_NAME, VAR_PROV_KEY,
			   (char *)prov_key,
			   KEY_LEN + 1) == WM_SUCCESS) {
		if (strlen((char *)prov_key) == KEY_LEN) {
			dbg("Using key from psm %s", prov_key);
			prov_ezconn_set_device_key(prov_key, KEY_LEN);
			return KEY_LEN;
		} else {
			dbg("Found incorrect prov_key. Starting provisioning"
			    " without key");
			dbg("You can set 16byte key using below command and "
			    "reboot the board");
			dbg("psm-set network prov_key <16byte key>");
			memset(prov_key, 0 , KEY_LEN);
			return 0;
		}
	} else {
		dbg("No prov_key found. Starting provisioning without key");
		return 0;
	}
}

/*
 * A simple HTTP Web-Service Handler
 *
 * Returns the string "Hello World" when a GET on http://<IP>/hello
 * is done.
 */

char *hello_world_string = "Hello World\n";

int hello_handler(httpd_request_t *req)
{
	char *content = hello_world_string;
	uint8_t my_mac[6];
	wlan_get_mac_address(my_mac);
	char deviceName[23];
	char buff[128];
	json_str_init(&jstr,buff,sizeof(buff),0);
	json_start_object(&jstr);
	
	snprintf(deviceName, sizeof(deviceName),
							 "ck00345678%02X%02X%02X%02X%02X%02X", my_mac[0], my_mac[1],my_mac[2], my_mac[3],my_mac[4], my_mac[5]);
	json_set_val_str(&jstr,"snId",deviceName);
	json_close_object(&jstr);			
	httpd_send_response(req, HTTP_RES_200,
			    buff, strlen(buff),
			    HTTP_CONTENT_PLAIN_TEXT_STR);
	return WM_SUCCESS;
}

static int wm_demo_get_ui_link(httpd_request_t *req)
{
	return cloud_get_ui_link(req);
}

int set_dev_seckey(httpd_request_t *req)
{
	char http_dev_seckey[64];
	char dev_seckey[64];
	int ret;

	ret = httpd_get_data_json(req, http_dev_seckey,
			sizeof(http_dev_seckey), &obj);
	//ret = httpd_get_data(req, http_dev_session_key,
			//sizeof(http_dev_session_key));
	if (ret < 0) {
		dbg("Failed to get post request data");
		return ret;
	}
	dbg("http get : %s",http_dev_seckey);
	if (json_get_val_str(&obj, "secKey",dev_seckey,sizeof(dev_seckey)) != WM_SUCCESS)
	{
		ret = httpd_send_response(req, HTTP_RES_200, HTTPD_JSON_ERROR,
			strlen(HTTPD_JSON_ERROR), HTTP_CONTENT_JSON_STR);
		
	}		
	else
	{
		ret = httpd_send_response(req, HTTP_RES_200, HTTPD_JSON_SUCCESS,
			strlen(HTTPD_JSON_SUCCESS), HTTP_CONTENT_JSON_STR);
		dbg("recvd seckey is %s",dev_seckey);
		hp_mdns_deannounce(net_get_uap_handle());
	}
	return ret;			

}


struct httpd_wsgi_call wm_demo_http_handlers[] = {
	{"/hello", HTTPD_DEFAULT_HDR_FLAGS, 0,
	hello_handler, set_dev_seckey, NULL, NULL},
	{"/cloud_ui", HTTPD_DEFAULT_HDR_FLAGS | HTTPD_HDR_ADD_PRAGMA_NO_CACHE,
	0, wm_demo_get_ui_link, NULL, NULL, NULL},
};

static int wm_demo_handlers_no =
	sizeof(wm_demo_http_handlers) / sizeof(struct httpd_wsgi_call);


/* This function is defined for handling critical error.
 * For this application, we just stall and do nothing when
 * a critical error occurs.
 *
 */
void appln_critical_error_handler(void *data)
{
	while (1)
		;
	/* do nothing -- stall */
}

/*
 * Register Web-Service handlers
 *
 */
int register_httpd_handlers()
{
	return httpd_register_wsgi_handlers(wm_demo_http_handlers,
		wm_demo_handlers_no);
}

/* This function must initialize the variables required (network name,
 * passphrase, etc.) It should also register all the event handlers that are of
 * interest to the application.
 */
int appln_config_init()
{
	/* Initialize service name for mdns */
	snprintf(appln_cfg.servname, MAX_SRVNAME_LEN, "TuyaSmart");
	appln_cfg.wps_pb_gpio = board_button_1();
	/* Initialize reset to provisioning push button settings */
	appln_cfg.reset_prov_pb_gpio = board_button_3();
	/* Initialize power management */
	hp_pm_init();
	return 0;
}

/*-----------------------Local declarations----------------------*/
static int provisioned;
static uint8_t mdns_announced;

/* This function stops various services when
 * device gets disconnected or reset to provisioning is done.
 */
static void stop_services()
{
	wm_demo_cloud_stop();
	led_off(board_led_1());
}

/* This function starts various services when
 * device get connected to a network.
 */


 


 //http_session_t * test_handle;
static void start_services()
{
	dbg("Start Cloud");
	//wm_demo_cloud_start();
	
	//----------------------------------	
	int timeout = 5000;	//  ms
	int status = http_open_session(&c.hS, "192.168.0.19:8089", 0,NULL,0);
	cl_dbg("http_open_session status: %d",status);

	/* Set timeout on cloud socket	*/
	//http_setsockopt(&c.hS, 0xfff, 0x1006,
	//	&timeout, sizeof(int));

	//char buff[900];
	//
	//	 http_lowlevel_read(c.hS,buff,sizeof(buff));
	//	 dbg("recv : %s",buff);
		 
//	os_thread_create(&app_thread_http_listen, /* thread handle */
//				"http_listen", /* thread name */
//				http_listen,	/* entry function */
//				0,	/* argument */
//				&app_stack_http_listen,	/* stack */
//				OS_PRIO_2); /* priority - medium low */

}
/*
 * Event: INIT_DONE
 *
 * The application framework is initialized.
 *
 * The data field has information passed by boot2 bootloader
 * when it loaded the application.
 *
 * ?? What happens if app is loaded via jtag
 */
static void event_init_done(void *data)
{
#if APPCONFIG_DEBUG_ENABLE
	struct app_init_state *state;
	state = (struct app_init_state *)data;
#endif /* APPCONFIG_DEBUG_ENABLE */

	dbg("Event: INIT_DONE");
	dbg("Factory reset bit status: %d", state->factory_reset);
	dbg("Booting from backup firmware status: %d", state->backup_fw);
	dbg("Previous reboot cause: %u", state->rst_cause);

	int err = os_timer_create(&uap_down_timer,
				  "uap-down-timer",
				  os_msec_to_ticks(UAP_DOWN_TIMEOUT),
				  &uap_down_timer_cb,
				  NULL,
				  OS_TIMER_ONE_SHOT,
				  OS_TIMER_NO_ACTIVATE);
	if (err != WM_SUCCESS) {
		dbg("Unable to start uap down timer");
	}
}

/*
 * Handler invoked on WLAN_INIT_DONE event.
 *
 * When WLAN is started, the application framework looks to
 * see whether a home network information is configured
 * and stored in PSM (persistent storage module).
 *
 * The data field returns whether a home network is provisioned
 * or not, which is used to determine what network interfaces
 * to start (station, micro-ap, or both).
 *
 * If provisioned, the station interface of the device is
 * connected to the configured network.
 *
 * Else, Micro-AP network is configured.
 *
 * (If desired, the Micro-AP network can also be started
 * along with the station interface.)
 *
 * We also start all the services which don't need to be
 * restarted between provisioned and non-provisioned mode
 * or between connected and disconnected state.
 *
 * Accordingly:
 *      -- Start mDNS and advertize services
 *	-- Start HTTP Server
 *	-- Register WSGI handlers for HTTP server
 */
static void event_wlan_init_done(void *data)
{
	int ret;
	/* We receive provisioning status in data */
	provisioned = (int)data;

	dbg("Event: WLAN_INIT_DONE provisioned=%d", provisioned);

	/* Initialize ssid to be used for uAP mode */
	appln_init_ssid();

	if (provisioned) {
		app_sta_start();
		/* Load  CLOUD overlay in memory */
		wm_demo_load_cloud_overlay();
	} else {
	if(PROV_EZCONNECT)
	{
		app_uap_start_with_dhcp(appln_cfg.ssid, appln_cfg.passphrase);
		wm_demo_load_wps_overlay();
	}
	else
	{
		int keylen = wmdemo_get_prov_key(prov_key);
		app_ezconnect_provisioning_start(prov_key, keylen);
	}
	/*
#ifndef APPCONFIG_PROV_EZCONNECT
		app_uap_start_with_dhcp(appln_cfg.ssid, appln_cfg.passphrase);
		// Load WPS overlay in memory 
		wm_demo_load_wps_overlay();
#else
		//app_uap_start_with_dhcp(appln_cfg.ssid, appln_cfg.passphrase);
		int keylen = wmdemo_get_prov_key(prov_key);
		app_ezconnect_provisioning_start(prov_key, keylen);
		//app_ezconnect_provisioning_start("1234123412341234", 16);
#endif
*/
	}

	if (provisioned)
		hp_configure_reset_prov_pushbutton();

#if APPCONFIG_MDNS_ENABLE
	/*
	 * Start mDNS and advertize our hostname using mDNS
	 */
	dbg("Starting mdns");
	app_mdns_start(appln_cfg.hostname);
#endif /* APPCONFIG_MDNS_ENABLE */

	/*
	 * Start http server and enable webapp in the
	 * FTFS partition on flash
	 */
	ret = app_httpd_with_fs_start(ftfs_api_version, ftfs_part_name, &fs);
	if (ret != WM_SUCCESS)
		dbg("Error: Failed to start HTTPD");

	/*
	 * Register /hello http handler
	 */
	register_httpd_handlers();

	/*
	 * Initialize CLI Commands for some of the modules:
	 *
	 * -- psm:  allows user to check data in psm partitions
	 * -- ftfs: allows user to see contents of ftfs
	 * -- wlan: allows user to explore basic wlan functions
	 */

	ret = psm_cli_init();
	if (ret != WM_SUCCESS)
		dbg("Error: psm_cli_init failed");
	ret = ftfs_cli_init(fs);
	if (ret != WM_SUCCESS)
		dbg("Error: ftfs_cli_init failed");
	ret = rfget_cli_init();
	if (ret != WM_SUCCESS)
		dbg("Error: rfget_cli_init failed");
	ret = wlan_cli_init();
	if (ret != WM_SUCCESS)
		dbg("Error: wlan_cli_init failed");

	if (!provisioned) {
		/* Start Slow Blink */
		led_slow_blink(board_led_2());
	}

}

/*
 * Event: Micro-AP Started
 *
 * If we are not provisioned, then start provisioning on
 * the Micro-AP network.
 *
 * Also, enable WPS.
 *
 * Since Micro-AP interface is UP, announce mDNS service
 * on the Micro-AP interface.
 */
static void event_uap_started(void *data)
{
if(PROV_EZCONNECT)
{
	void *iface_handle = net_get_uap_handle();

	dbg("Event: Micro-AP Started");
	if (!provisioned) {
		dbg("Starting provisioning");
#if APPCONFIG_WPS_ENABLE
		hp_configure_wps_pushbutton();
		wm_demo_wps_cli_init();
		app_provisioning_start(PROVISIONING_WLANNW |
				       PROVISIONING_WPS);
#else
		app_provisioning_start(PROVISIONING_WLANNW);
#endif /* APPCONFIG_WPS_ENABLE */

	}
	hp_mdns_announce(iface_handle, UP);
}
}

/*
 * Event: PROV_DONE
 *
 * Provisioning is complete. We can stop the provisioning
 * service.
 *
 * Stop WPS.
 *
 * Enable Reset to Prov Button.
 */
static void event_prov_done(void *data)
{
	hp_configure_reset_prov_pushbutton();
if(PROV_EZCONNECT)
{
#if APPCONFIG_WPS_ENABLE
	hp_unconfigure_wps_pushbutton();
#endif /* APPCONFIG_WPS_ENABLE */
	wm_demo_wps_cli_deinit();
	app_provisioning_stop();
}else{
	app_ezconnect_provisioning_stop();
 /* APPCONFIG_PROV_EZCONNECT */
	dbg("Provisioning successful");
}
}

/* Event: PROV_CLIENT_DONE
 *
 * Provisioning Client has terminated session.
 *
 * We can now safely stop the Micro-AP network.
 *
 * Note: It is possible to keep the Micro-AP network alive even
 * when the provisioning client is done.
 */
static void event_prov_client_done(void *data)
{

	int ret;
	if(PROV_EZCONNECT)
	{
		hp_mdns_deannounce(net_get_uap_handle());
		ret = app_uap_stop();
		if (ret != WM_SUCCESS)
			dbg("Error: Failed to Stop Micro-AP");
	}
}

/*
 * Event UAP_STOPPED
 *
 * Normally, we will get here when provisioning is complete,
 * and the Micro-AP network is brought down.
 *
 * If we are connected to an AP, we can enable IEEE Power Save
 * mode here.
 */
static void event_uap_stopped(void *data)
{
	dbg("Event: Micro-AP Stopped");
	hp_pm_wifi_ps_enable();
}

/*
 * Event: PROV_WPS_SSID_SELECT_REQ
 *
 * An SSID with active WPS session is found and WPS negotiation will
 * be started with this AP.
 *
 * Since WPS take a lot of memory resources (on the heap), we
 * temporarily stop http server (and, the Micro-AP provisioning
 * along with it).
 *
 * The HTTP server will be restarted when WPS session is over.
 */
static void event_prov_wps_ssid_select_req(void *data)
{
	int ret;

	ret = app_httpd_stop();
	if (ret != WM_SUCCESS) {
		dbg("Error stopping HTTP server");
	}
}

/*
 * Event: PROV_WPS_SUCCESSFUL
 *
 * WPS session completed successfully.
 *
 * Restart the HTTP server that was stopped when WPS session attempt
 * began.
 */
static void event_prov_wps_successful(void *data)
{
	int ret;

	ret = app_httpd_with_fs_start(ftfs_api_version, ftfs_part_name, &fs);
	if (ret != WM_SUCCESS) {
		dbg("Error starting HTTP server");
	}

	return;
}

/*
 * Event: PROV_WPS_UNSUCCESSFUL
 *
 * WPS session completed unsuccessfully.
 *
 * Restart the HTTP server that was stopped when WPS session attempt
 * began.
 */
static void event_prov_wps_unsuccessful(void *data)
{
	int ret;

	ret = app_httpd_with_fs_start(ftfs_api_version, ftfs_part_name, &fs);
	if (ret != WM_SUCCESS) {
		dbg("Error starting HTTP server");
	}
	return;
}

/*
 * Event: CONNECTING
 *
 * We are attempting to connect to the Home Network
 *
 * Note: We can come here:
 *
 *   1. After boot -- if already provisioned.
 *   2. After provisioning
 *   3. After link loss
 *
 * This is just a transient state as we will either get
 * CONNECTED or have a CONNECTION/AUTH Failure.
 *
 */
static void event_normal_connecting(void *data)
{
	net_dhcp_hostname_set(appln_cfg.hostname);
	dbg("Connecting to Home Network");
	/* Start Fast Blink */
	led_fast_blink(board_led_2());
}

/* Event: AF_EVT_NORMAL_CONNECTED
 *
 * Station interface connected to home AP.
 *
 * Network dependent services can be started here. Note that these
 * services need to be stopped on disconnection and
 * reset-to-provisioning event.
 */
static void event_normal_connected(void *data)
{
	void *iface_handle;
	char ip[16];

	led_off(board_led_2());

	led_on(board_led_1());

	app_network_ip_get(ip);
	dbg("Connected to Home Network with IP address = %s", ip);

	iface_handle = net_get_sta_handle();
	if (!mdns_announced) {
		hp_mdns_announce(iface_handle, UP);
		mdns_announced = 1;
	} else {
		hp_mdns_down_up(iface_handle);
	}
	/* Load CLOUD overlay in memory */
	wm_demo_load_cloud_overlay();
	start_services();
	/*
	 * If micro AP interface is up
	 * queue a timer which will take
	 * micro AP interface down.
	 */
	if (is_uap_started()) {
		os_timer_activate(&uap_down_timer);
		return;
	}

	//hp_pm_wifi_ps_enable();
}

/*
 * Event: CONNECT_FAILED
 *
 * We attempted to connect to the Home AP, but the connection did
 * not succeed.
 *
 * This typically indicates:
 *
 * -- Authentication failed.
 * -- The access point could not be found.
 * -- We did not get a valid IP address from the AP
 *
 */
static void event_connect_failed(void *data)
{
	char failure_reason[32];

	if (*(app_conn_failure_reason_t *)data == AUTH_FAILED)
		strcpy(failure_reason, "Authentication failure");
	if (*(app_conn_failure_reason_t *)data == NETWORK_NOT_FOUND)
		strcpy(failure_reason, "Network not found");
	if (*(app_conn_failure_reason_t *)data == DHCP_FAILED)
		strcpy(failure_reason, "DHCP failure");

	os_thread_sleep(os_msec_to_ticks(2000));
	dbg("Application Error: Connection Failed: %s", failure_reason);
	led_off(board_led_1());
}

/*
 * Event: USER_DISCONNECT
 *
 * This means that the application has explicitly requested a network
 * disconnect
 *
 */
static void event_normal_user_disconnect(void *data)
{
	led_off(board_led_1());
	dbg("User disconnect");
}

/*
 * Event: LINK LOSS
 *
 * We lost connection to the AP.
 *
 * The App Framework will attempt to reconnect. We dont
 * need to do anything here.
 */
static void event_normal_link_lost(void *data)
{
	dbg("Link Lost");
}

static void event_normal_pre_reset_prov(void *data)
{
	hp_mdns_deannounce(net_get_mlan_handle());
}

static void event_normal_dhcp_renew(void *data)
{
	void *iface_handle = net_get_mlan_handle();
	hp_mdns_announce(iface_handle, REANNOUNCE);
}

static void event_normal_reset_prov(void *data)
{
	led_on(board_led_2());
	/* Start Slow Blink */
	led_slow_blink(board_led_2());

	/* Stop services like cloud */
	stop_services();

	/* Cancel the UAP down timer timer */
	os_timer_deactivate(&uap_down_timer);

	hp_pm_wifi_ps_disable();
	/* Load WPS overlay in memory */
	wm_demo_load_wps_overlay();

	/* Reset to provisioning */
	provisioned = 0;
	mdns_announced = 0;
	hp_unconfigure_reset_prov_pushbutton();
if(PROV_EZCONNECT)
{
	if (is_uap_started() == false) {
		app_uap_start_with_dhcp(appln_cfg.ssid, appln_cfg.passphrase);
	} else {
#ifdef APPCONFIG_WPS_ENABLE
		hp_configure_wps_pushbutton();
		wm_demo_wps_cli_init();
		app_provisioning_start(PROVISIONING_WLANNW |
				       PROVISIONING_WPS);
#else
		app_provisioning_start(PROVISIONING_WLANNW);
#endif /* APPCONFIG_WPS_ENABLE */
	}
}
else
{
	int keylen = wmdemo_get_prov_key(prov_key);
	app_ezconnect_provisioning_start(prov_key, keylen);
}
}
void ps_state_to_desc(char *ps_state_desc, int ps_state)
{
	switch (ps_state) {
	case WLAN_IEEE:
		strcpy(ps_state_desc, "IEEE PS");
		break;
	case WLAN_DEEP_SLEEP:
		strcpy(ps_state_desc, "Deep sleep");
		break;
	case WLAN_PDN:
		strcpy(ps_state_desc, "Power down");
		break;
	case WLAN_ACTIVE:
		strcpy(ps_state_desc, "WLAN Active");
		break;
	default:
		strcpy(ps_state_desc, "Unknown");
		break;
	}
}

/*
 * Event: PS ENTER
 *
 * Application framework event to indicate the user that WIFI
 * has entered Power save mode.
 */

static void event_ps_enter(void *data)
{
	int ps_state = (int) data;
	char ps_state_desc[32];
	if (ps_state == WLAN_PDN) {
		dbg("NOTE: Due to un-availability of "
		    "enough dynamic memory for ");
		dbg("de-compression of WLAN Firmware, "
		    "exit from PDn will not\r\nwork with wm_demo.");
		dbg("Instead of wm_demo, pm_mc200_wifi_demo"
		    " application demonstrates\r\nthe seamless"
		    " exit from PDn functionality today.");
		dbg("This will be fixed in subsequent "
		    "software release.\r\n");
	}
	ps_state_to_desc(ps_state_desc, ps_state);
	dbg("Power save enter : %s", ps_state_desc);

}

/*
 * Event: PS EXIT
 *
 * Application framework event to indicate the user that WIFI
 * has exited Power save mode.
 */

static void event_ps_exit(void *data)
{
	int ps_state = (int) data;
	char ps_state_desc[32];
	ps_state_to_desc(ps_state_desc, ps_state);
	dbg("Power save exit : %s", ps_state_desc);
}

/* This is the main event handler for this project. The application framework
 * calls this function in response to the various events in the system.
 */
int common_event_handler(int event, void *data)
{
	switch (event) {
	case AF_EVT_INIT_DONE:
		event_init_done(data);
		break;
	case AF_EVT_WLAN_INIT_DONE:
		event_wlan_init_done(data);
		break;
	case AF_EVT_NORMAL_CONNECTING:
		event_normal_connecting(data);
		break;
	case AF_EVT_NORMAL_CONNECTED:
		event_normal_connected(data);
		break;
	case AF_EVT_NORMAL_CONNECT_FAILED:
		event_connect_failed(data);
		break;
	case AF_EVT_NORMAL_LINK_LOST:
		event_normal_link_lost(data);
		break;
	case AF_EVT_NORMAL_USER_DISCONNECT:
		event_normal_user_disconnect(data);
		break;
	case AF_EVT_NORMAL_DHCP_RENEW:
		event_normal_dhcp_renew(data);
		break;
	case AF_EVT_PROV_WPS_SSID_SELECT_REQ:
		event_prov_wps_ssid_select_req(data);
		break;
	case AF_EVT_PROV_WPS_SUCCESSFUL:
		event_prov_wps_successful(data);
		break;
	case AF_EVT_PROV_WPS_UNSUCCESSFUL:
		event_prov_wps_unsuccessful(data);
		break;
	case AF_EVT_NORMAL_PRE_RESET_PROV:
		event_normal_pre_reset_prov(data);
		break;
	case AF_EVT_NORMAL_RESET_PROV:
		event_normal_reset_prov(data);
		break;
	case AF_EVT_UAP_STARTED:
		event_uap_started(data);
		break;
	case AF_EVT_UAP_STOPPED:
		event_uap_stopped(data);
		break;
	case AF_EVT_PROV_DONE:
		event_prov_done(data);
		break;
	case AF_EVT_PROV_CLIENT_DONE:
		event_prov_client_done(data);
		break;
	case AF_EVT_PS_ENTER:
		event_ps_enter(data);
		break;
	case AF_EVT_PS_EXIT:
		event_ps_exit(data);
		break;
	default:
		break;
	}

	return 0;
}

static void modules_init()
{
	int ret;

	ret = wmstdio_init(UART0_ID, 0);
	if (ret != WM_SUCCESS) {
		dbg("Error: wmstdio_init failed");
		appln_critical_error_handler((void *) -WM_FAIL);
	}

	ret = cli_init();
	if (ret != WM_SUCCESS) {
		dbg("Error: cli_init failed");
		appln_critical_error_handler((void *) -WM_FAIL);
	}
	/* Initialize time subsystem.
	 *
	 * Initializes time to 1/1/1970 epoch 0.
	 */
	ret = wmtime_init();
	if (ret != WM_SUCCESS) {
		dbg("Error: wmtime_init failed");
		appln_critical_error_handler((void *) -WM_FAIL);
	}

	/*
	 * Register Time CLI Commands
	 */
	ret = wmtime_cli_init();
	if (ret != WM_SUCCESS) {
		dbg("Error: wmtime_cli_init failed");
		appln_critical_error_handler((void *) -WM_FAIL);
	}

	/*
	 * Initialize Power Management Subsystem
	 */
	ret = pm_init();
	if (ret != WM_SUCCESS) {
		dbg("Error: pm_init failed");
		appln_critical_error_handler((void *) -WM_FAIL);
	}

	/*
	 * Register Power Management CLI Commands
	 */
	ret = pm_cli_init();
	if (ret != WM_SUCCESS) {
		dbg("Error: pm_cli_init failed");
		appln_critical_error_handler((void *) -WM_FAIL);
	}

	ret = pm_mc200_cli_init();
	if (ret != WM_SUCCESS) {
		dbg("Error: pm_mc200_cli_init failed");
		appln_critical_error_handler((void *) -WM_FAIL);
	}

	ret = gpio_drv_init();
	if (ret != WM_SUCCESS) {
		dbg("Error: gpio_drv_init failed");
		appln_critical_error_handler((void *) -WM_FAIL);
	}

	ret = healthmon_init();
	if (ret != WM_SUCCESS) {
		dbg("Error: healthmon_init failed");
		appln_critical_error_handler((void *) -WM_FAIL);
	}
	ret = healthmon_start();
	if (ret != WM_SUCCESS) {
		dbg("Error: healthmon_start failed");
		appln_critical_error_handler((void *) -WM_FAIL);
	}
	/* Set the final_about_to_die handler of the healthmon */
	healthmon_set_final_about_to_die_handler
		((void (*)())diagnostics_write_stats);

	app_sys_register_diag_handler();
	app_sys_register_upgrade_handler();
	return;
}


/*------------------Macro Definitions ------------------*/

#define GPIO_LED_FN  PINMUX_FUNCTION_0
#define GPIO_PUSHBUTTON_FN PINMUX_FUNCTION_0

/*------------------Global Variable Definitions ---------*/

/* This holds  LED gpio pin number */
static unsigned int gpio_led;
/* This holds Pushbutton gpio pin number */
static unsigned int gpio_pushbutton;
/* This indicates the state of LED on or off */
static unsigned int gpio_led_state;

/* This function turns on the LED*/
static void gpio_led_on(void)
{
	mdev_t *gpio_dev = gpio_drv_open("MDEV_GPIO");
	/* Turn on LED by writing  0 in GPIO register */
	gpio_drv_write(gpio_dev, gpio_led, 0);
	gpio_drv_close(gpio_dev);
	gpio_led_state = 1;
}

/* This function turns off the LED*/
static void gpio_led_off(void)
{
	mdev_t *gpio_dev = gpio_drv_open("MDEV_GPIO");
	/* Turn off LED by writing  1 in GPIO register */
	gpio_drv_write(gpio_dev, gpio_led, 1);
	gpio_drv_close(gpio_dev);
	gpio_led_state = 0;
}

/* This function is called when push button is pressed*/
static void pushbutton_cb()
{
	/*
	int state;
	mdev_t  *gpio_dev = gpio_drv_open("MDEV_GPIO");	
	gpio_drv_read(gpio_dev,gpio_pushbutton,&state);	
	while(state==GPIO_IO_LOW)
	{
		gpio_drv_read(gpio_dev,gpio_pushbutton,&state);		
	}
	gpio_drv_close(gpio_dev);
	
	
	if (gpio_led_state)
		gpio_led_off();
	else
		gpio_led_on();*/
	os_semaphore_put(&button_sem);

	
}

/* Configure GPIO pins to be used as LED and push button */
static void configure_gpios()
{
	mdev_t *pinmux_dev, *gpio_dev;

	/* Initialize  pinmux driver */
	pinmux_drv_init();

	/* Open pinmux driver */
	pinmux_dev = pinmux_drv_open("MDEV_PINMUX");

	/* Initialize GPIO driver */
	gpio_drv_init();

	/* Open GPIO driver */
	gpio_dev = gpio_drv_open("MDEV_GPIO");

	/* Configure GPIO pin function for GPIO connected to LED */
	pinmux_drv_setfunc(pinmux_dev, gpio_led, GPIO_LED_FN);

	/* Confiugre GPIO pin direction as Output */
	gpio_drv_setdir(gpio_dev, gpio_led, GPIO_OUTPUT);
	gpio_drv_write(gpio_dev, gpio_led, 1);

	/* Configure GPIO pin function for GPIO connected to push button */
	pinmux_drv_setfunc(pinmux_dev, gpio_pushbutton, GPIO_PUSHBUTTON_FN);

	/* Confiugre GPIO pin direction as input */
	gpio_drv_setdir(gpio_dev, gpio_pushbutton, GPIO_INPUT);

	/* Register a callback for push button interrupt */
	gpio_drv_set_cb(gpio_dev, gpio_pushbutton, GPIO_INT_FALLING_EDGE,
			pushbutton_cb);

	/* Close drivers */
	pinmux_drv_close(pinmux_dev);
	gpio_drv_close(gpio_dev);
}

static void report2cloud()
{
	time_t time;
	char deviceName[24];
	uint8_t my_mac[6];	
	struct json_str jstr;
	char buff[512];

	
	wlan_get_mac_address(my_mac);
	snprintf(deviceName, sizeof(deviceName),
				 "ck00345678%02X%02X%02X%02X%02X%02X", my_mac[0], my_mac[1],my_mac[2], my_mac[3],my_mac[4], my_mac[5]);

	json_str_init(&jstr,buff,sizeof(buff),0);
	json_start_object(&jstr);
	json_set_val_str(&jstr,"a","report");
	json_push_object(&jstr, "d");
	json_push_array_object(&jstr, "pl");

	json_start_object(&jstr);
	json_set_val_int(&jstr,"onOff",gpio_led_state);
	json_set_val_int(&jstr,"pid",0);
	json_close_object(&jstr);
	json_pop_array_object(&jstr);
	json_pop_object(&jstr);
	json_set_val_str(&jstr,"id",deviceName);
	time = wmtime_time_get_posix();
	json_set_val_int(&jstr, "t", time);
	json_close_object(&jstr);
	strcat(buff,"\r\n");
	http_lowlevel_write(c.hS,buff,strlen(buff));

	

}

static void button_click()
{
	int state;
	int cycle_cnt;
	int low_cnt;
	mdev_t  *gpio_dev = gpio_drv_open("MDEV_GPIO");	
	time_t time;
	char deviceName[24];
	uint8_t my_mac[6];
	
	

	
	while(1){
		os_semaphore_get(&button_sem, OS_WAIT_FOREVER);
		cycle_cnt = 0;
		low_cnt =0;
		gpio_drv_read(gpio_dev,gpio_pushbutton,&state);
		if(state==GPIO_IO_LOW)	
		{
			while(cycle_cnt<5)
			{
				gpio_drv_read(gpio_dev,gpio_pushbutton,&state);
				if(state == GPIO_IO_HIGH)
					cycle_cnt ++;
				else
				{
					cycle_cnt = 0;
					low_cnt++;
				}

				
				if(low_cnt==50*5)
				{						
					led_on(board_led_2());
					led_slow_blink(board_led_2());
					PROV_EZCONNECT =0;
				}
				if(low_cnt==50*10)
				{						
					led_off(board_led_2());
					led_on(board_led_1());
					led_slow_blink(board_led_1());
					PROV_EZCONNECT =1;
				}
				
				os_thread_sleep(os_msec_to_ticks(20));
			}
			
			
			if(low_cnt>50*5)
			{
				//app_provisioning_stop();
				//app_ezconnect_provisioning_stop();
				app_reset_configured_network();
			}
			
			
			if(low_cnt < 5*5)
			{
				if (gpio_led_state)
					gpio_led_off();
				else
					gpio_led_on();

				report2cloud();
				
				/*
				struct json_str jstr;
				char buff[512];
				json_str_init(&jstr,buff,sizeof(buff),0);
				json_start_object(&jstr);
				//json_set_val_str(&jstr,"device","switch");
				wlan_get_mac_address(my_mac);
				snprintf(deviceName, sizeof(deviceName),
							 "TUYACNXXXX%02X%02X%02X%02X%02X%02X", my_mac[0], my_mac[1],my_mac[2], my_mac[3],my_mac[4], my_mac[5]);
				json_set_val_str(&jstr,"snId",deviceName);			
				json_set_val_int(&jstr,"value",gpio_led_state);
				

				time = wmtime_time_get_posix();
				json_set_val_int(&jstr, J_NAME_TIME, time);
	 
				json_close_object(&jstr);
				http_lowlevel_write(c.hS,buff,strlen(buff));
				*/
			}
							
				
			
		}
		
	}
	gpio_drv_close(gpio_dev);
}

static void http_listen()
 {
	 char buff[512];
	 //char buff_array[128];
	 struct json_object json_obj;
	 struct json_object array_json_obj;
	 int led_val;
	 char status[5];
	 time_t time;
	 int array_val;
	 json_object_init(&json_obj, buff);
	 while(1)
	 {
		if(c.hS)
		{
			memset(buff,0,sizeof(buff));
			if(http_lowlevel_read(c.hS,buff,sizeof(buff))>0)
			{
				dbg("recv : %s",buff);
				
				
				if(json_get_val_int(&json_obj, "t", (int *)&time) == WM_SUCCESS)
				{
					wmtime_time_set_posix(time);
					dbg("time : %d",time);
				}
				
				json_get_array_object(&json_obj, "pl");	
				if(json_obj.array_obj>0)
				{
					json_object_init(&array_json_obj, (char *)(buff + json_obj.array_obj +1));
					dbg("array : %d,curr : %d",json_obj.array_obj,json_obj.current_obj);
					
					if(json_get_val_int(&array_json_obj, "onOff", &led_val) == WM_SUCCESS)
					{
						if (led_val)
							gpio_led_on();
						else
							gpio_led_off();
					}
				}

				
				//{"data":true,"status":"ok","t":1426857651}
				//{"code":"PARAMS_ILLEGAL_OR_DATA_ILLEGAL","status":"error"}
				json_get_val_str(&json_obj, "status",status,sizeof(status));
				dbg("recv : %d,%s",led_val,status);				
			}
		}
		 os_thread_sleep(os_msec_to_ticks(1000));
	 }
 }


int main()
{
	modules_init();

	dbg("Build Time: " __DATE__ " " __TIME__ "");

	appln_config_init();

	int status = os_semaphore_create(&button_sem, "button");
	
	if (status != WM_SUCCESS) {
		wmprintf("Unable to create sem\r\n");
		return 0;
	}

	os_semaphore_get(&button_sem, OS_WAIT_FOREVER);

	/* Create the main application thread */
	os_thread_create(&app_thread_button, /* thread handle */
			"button_click", /* thread name */
			button_click,	/* entry function */
			0,	/* argument */
			&app_stack_button,	/* stack */
			OS_PRIO_1); /* priority - medium low */
	
	os_thread_create(&app_thread_http_listen, /* thread handle */
				"http_listen", /* thread name */
				http_listen,	/* entry function */
				0,	/* argument */
				&app_stack_http_listen,	/* stack */
				OS_PRIO_3); /* priority - medium low */

	gpio_led = board_led_3();
	gpio_pushbutton = board_button_1();
	configure_gpios();



	/* Start the application framework */
	if (app_framework_start(common_event_handler) != WM_SUCCESS) {
		dbg("Failed to start application framework");
		appln_critical_error_handler((void *) -WM_FAIL);
	}
	return 0;
}
