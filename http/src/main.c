/*
 *  Copyright (C) 2008-2013, Marvell International Ltd.
 *  All Rights Reserved.
 */

/*
 * Simple WLAN Application using micro-AP
 *
 * Summary:
 *
 * This application starts a micro-AP network. It announce a mDNS/DNS-SD service
 * on this network. It also starts a Web Server and makes available a web URI
 * http://192.168.10.1/hello.
 *
 * Description:
 *
 * The application is written using Application Framework that
 * simplifies development of WLAN networking applications.
 *
 *
 * WLAN Initialization:
 *
 * When the application framework is started, it starts up the WLAN
 * sub-system and initializes the network stack. The app receives the event
 * when the WLAN subsystem has been started and initialized.
 *
 * The app starts a micro-AP network along with a DHCP server. This will
 * create a WLAN network and also creates a IP network interface
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
 * By default, there are no handlers defined, and any request to the
 * web-server will result in a 404 error from the web-server.
 *
 * We also register a simple Handler with the Web-Server that will
 * respond with Text String "Hello World" at URI http://192.168.10.1/hello
 *
 */
#include <wm_os.h>
#include <app_framework.h>
#include <wmtime.h>
#include <partition.h>
#include <appln_cb.h>
#include <appln_dbg.h>
#include <cli.h>
#include <psm.h>
#include <wmstdio.h>
#include <wmsysinfo.h>
#include <wm_net.h>
#include <httpd.h>

#include <wm_os.h>
#include <mdev_gpio.h>
#include <mdev_pinmux.h>
#include <mc200/mc200_gpio.h>
#include <board.h>


/*------------------Macro Definitions ------------------*/
#define GPIO_LED_FN  PINMUX_FUNCTION_0

/*------------------Global variables declarations--------*/
appln_config_t appln_cfg = {
	.ssid = "Default",
	.passphrase = "marvellwm",
	.hostname = "uAPdemo"
};

/* This holds  LED gpio pin number */
static unsigned int gpio_led;
/* This indicates the state of LED on or off */
static unsigned int gpio_led_state;

/*-----------------------Global functions declarations----------------------*/
int appln_config_init();
void appln_critical_error_handler(void *data);
int hello_handler(httpd_request_t *req);
int register_httpd_handlers();
void event_uap_started(void *data);
void event_uap_stopped(void *data);
void event_wlan_init_done(void *data);
int common_event_handler(int event, void *data);
static void modules_init();

static void configure_gpios();
static void gpio_led_on(void);
static void gpio_led_off(void);
static void configure_gpios();

/*-----------------------Application Main entry point ----------------------*/
int main()
{
	modules_init();

	dbg("Build Time: " __DATE__ " " __TIME__ "");

	/* Configure LED */
	gpio_led = board_led_2();
	wmprintf(" LED Pin : %d\r\n", gpio_led);
	configure_gpios();

	/* Start the application framework */
	if (app_framework_start(common_event_handler) != WM_SUCCESS) {
		dbg("Failed to start application framework");
				appln_critical_error_handler((void *) -WM_FAIL);
	}
	return 0;
}

static void modules_init()
{
	int ret;

	/*
	 * Initialize wmstdio prints
	 */
	ret = wmstdio_init(UART0_ID, 0);
	if (ret != WM_SUCCESS) {
		dbg("Error: wmstdio_init failed");
		appln_critical_error_handler((void *) -WM_FAIL);
	}

	/*
	 * Initialize CLI Commands
	 */
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
	 * Register Power Management CLI Commands
	 */
	ret = pm_cli_init();
	if (ret != WM_SUCCESS) {
		dbg("Error: pm_cli_init failed");
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

	return;
}

/* This is the main event handler for this project. The application framework
 * calls this function in response to the various events in the system.
 */
int common_event_handler(int event, void *data)
{
	switch (event) {
	case AF_EVT_WLAN_INIT_DONE:
		dbg("WLAN Init Done Event Received\n");
		appln_config_init();
		event_wlan_init_done(data);
		break;
	case AF_EVT_UAP_STARTED:
		dbg("UAP Started Event Received\n");
		event_uap_started(data);
		break;
	case AF_EVT_UAP_STOPPED:
		dbg("UAP Stopped Event Received\n");
		event_uap_stopped(data);
		break;
	default:
		break;
	}

	return 0;
}

/* This function must initialize the variables required (network name,
 * passphrase, etc.) It should also register all the event handlers that are of
 * interest to the application.
 */
int appln_config_init()
{
        uint8_t my_mac[6];

        wlan_get_mac_address(my_mac);

        /* Provisioning SSID */
	snprintf(appln_cfg.ssid, MAX_SRVNAME_LEN, "tp-%02X%02X", my_mac[4], my_mac[5]);

        /* Provisioning Passphrase */
	snprintf(appln_cfg.passphrase, MAX_SRVNAME_LEN, "tapu12345");

	return 0;
}

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
 * Handler invoked when WLAN subsystem is ready.
 *
 * The app-framework tells the handler whether there is
 * valid network information stored in persistent memory.
 *
 * The handler can then chose to connect to the network.
 *
 * We ignore the data and just start a Micro-AP network
 * with DHCP service. This will allow a client device
 * to connect to us and receive a valid IP address via
 * DHCP.
 */
void event_wlan_init_done(void *data)
{
	int ret;
	/*
	 * Initialize CLI Commands for some of the modules:
	 *
	 * -- psm:  allows user to check data in psm partitions
	 * -- wlan: allows user to explore basic wlan functions
	 */

	ret = psm_cli_init();
	if (ret != WM_SUCCESS)
		dbg("Error: psm_cli_init failed");

	ret = wlan_cli_init();
	if (ret != WM_SUCCESS)
		dbg("Error: wlan_cli_init failed");

	/* Start DHCP Server */
	app_uap_start_with_dhcp(appln_cfg.ssid, appln_cfg.passphrase);
}


/*
 * A simple HTTP Web-Service Handler
 *
 * Returns the string "Hello World" when a GET on http://<IP>/hello
 * is done.
 */
char *hello_world_string = "<html><head></head><body><center><h1>Hello Tapu!!!</h1></body></html>\n";

int hello_handler(httpd_request_t *req)
{
	char *content = hello_world_string;
	httpd_send_response(req, HTTP_RES_200, content, strlen(content),"text/html");
	return WM_SUCCESS;
}

struct httpd_wsgi_call hello_wsgi_handler = {
	"/hello",
	HTTPD_DEFAULT_HDR_FLAGS,
	0,
	hello_handler,
	NULL,
	NULL,
	NULL
};

int led_get_handler(httpd_request_t *req)
{
	char *content = "Success";
	snprintf(content, 100, "LED State: %d", gpio_led_state);
	
	httpd_send_response(req, HTTP_RES_200, content, strlen(content),"text/plain");
	return WM_SUCCESS;
}

int led_post_handler(httpd_request_t *req)
{
	char state;
	char *responseOn = "Successfully switched on";
	char *responseOff = "Successfully switched off";

	httpd_get_data(req, &state, 1);

	if(state=='1') {
		gpio_led_on();
		httpd_send_response(req, HTTP_RES_200, responseOn, strlen(responseOn),"text/html");
	}
	else {
		gpio_led_off();
		httpd_send_response(req, HTTP_RES_200, responseOff, strlen(responseOff),"text/html");
	}

	return WM_SUCCESS;
}

struct httpd_wsgi_call led_wsgi_handler = {
	"/led",
	HTTPD_DEFAULT_HDR_FLAGS,
	0,
	led_get_handler,
	led_post_handler,
	NULL,
	NULL
};

/*
 * Register Web-Service handlers
 *
 */
int register_httpd_handlers()
{
	httpd_register_wsgi_handler(&hello_wsgi_handler);
	httpd_register_wsgi_handler(&led_wsgi_handler);
	return WM_SUCCESS;
}

/*
 * Handler invoked when the Micro-AP Network interface
 * is ready.
 *
 */
void event_uap_started(void *data)
{
	int ret;

	/* Start http server */
	ret = app_httpd_start();
	if (ret != WM_SUCCESS)
		dbg("Failed to start HTTPD");

	/* Register HTTP Handler */
	ret = register_httpd_handlers();
	if (ret != WM_SUCCESS)
		dbg("Failed to register HTTPD handlers");
}

void event_uap_stopped(void *data)
{
	dbg("uap interface stopped");
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

	/* Close drivers */
	pinmux_drv_close(pinmux_dev);
	gpio_drv_close(gpio_dev);
}

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



