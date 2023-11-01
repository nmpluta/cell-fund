/*
 * Copyright (c) 2022 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/net/socket.h>

#include <zephyr/logging/log.h>
#include <dk_buttons_and_leds.h>
#include <modem/nrf_modem_lib.h>
#include <modem/lte_lc.h>
#include <nrf_modem_gnss.h>
#define SERVER_HOSTNAME "nordicecho.westeurope.cloudapp.azure.com"
#define SERVER_PORT "2444"

#define MESSAGE_SIZE 256
#define MESSAGE_TO_SEND "Hello"

static struct nrf_modem_gnss_pvt_data_frame pvt_data_frame;

/** @brief The variable `ttff_start_time` is used to store the start time 
  * of the Time to First Fix (TTFF) process. TTFF is the time it takes for 
  * a GNSS receiver to acquire satellite signals and calculate a position fix. */
static int64_t ttff_start_time;

/** @brief This variable is used to keep track of whether the first fix 
  * has been obtained during the Time to First Fix (TTFF) process. */
static bool ttff_first_fix = false;

/* STEP 3.1 - Declare buffer to send data in */
static uint8_t gps_data[MESSAGE_SIZE];

static int sock;
static struct sockaddr_storage server;
static uint8_t recv_buf[MESSAGE_SIZE];

static K_SEM_DEFINE(lte_connected, 0, 1);

LOG_MODULE_REGISTER(Lesson6_Exercise2, LOG_LEVEL_INF);

static int server_resolve(void)
{
	int err;
	struct addrinfo *result;
	struct addrinfo hints = {
		.ai_family = AF_INET,
		.ai_socktype = SOCK_DGRAM
	};

	err = getaddrinfo(SERVER_HOSTNAME, SERVER_PORT, &hints, &result);
	if (err != 0) {
		LOG_INF("ERROR: getaddrinfo failed %d", err);
		return -EIO;
	}

	if (result == NULL) {
		LOG_INF("ERROR: Address not found");
		return -ENOENT;
	}

	struct sockaddr_in *server4 = ((struct sockaddr_in *)&server);
	server4->sin_addr.s_addr =
		((struct sockaddr_in *)result->ai_addr)->sin_addr.s_addr;
	server4->sin_family = AF_INET;
	server4->sin_port = ((struct sockaddr_in *)result->ai_addr)->sin_port;

	char ipv4_addr[NET_IPV4_ADDR_LEN];
	inet_ntop(AF_INET, &server4->sin_addr.s_addr, ipv4_addr,
		  sizeof(ipv4_addr));
	LOG_INF("IPv4 Address found %s", ipv4_addr);

	freeaddrinfo(result);

	return 0;
}

static int server_connect(void)
{
	int err;
	sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (sock < 0) {
		LOG_ERR("Failed to create socket: %d.", errno);
		return -errno;
	}

	err = connect(sock, (struct sockaddr *)&server,
		      sizeof(struct sockaddr_in));
	if (err < 0) {
		LOG_ERR("Connect failed : %d", errno);
		return -errno;
	}
	LOG_INF("Successfully connected");

	return 0;
}

static void lte_handler(const struct lte_lc_evt *const evt)
{
	switch (evt->type)
	{
		case LTE_LC_EVT_NW_REG_STATUS:
			if ((evt->nw_reg_status != LTE_LC_NW_REG_REGISTERED_HOME) &&
				(evt->nw_reg_status != LTE_LC_NW_REG_REGISTERED_ROAMING)) {
				break;
			}
			LOG_INF("Network registration status: %s",
					evt->nw_reg_status == LTE_LC_NW_REG_REGISTERED_HOME ?
					"Connected - home network" : "Connected - roaming");
			k_sem_give(&lte_connected);
			break;
		case LTE_LC_EVT_RRC_UPDATE:
			LOG_INF("RRC mode: %s",
					evt->rrc_mode == LTE_LC_RRC_MODE_CONNECTED ?
					"Connected" : "Idle");
			break;

		/* STEP 9.1 - On event PSM update, print PSM paramters and check if was enabled */
		case LTE_LC_EVT_PSM_UPDATE:
			LOG_INF("PSM parameter update: TAU: %d s, Active time: %d s",
					evt->psm_cfg.tau, evt->psm_cfg.active_time);
			
			if (evt->psm_cfg.active_time == -1)
			{
				LOG_ERR("Network rejected PSM request");
			}
			break;

		/* STEP 9.2 - On event eDRX update, print eDRX paramters */
		case LTE_LC_EVT_EDRX_UPDATE:
			LOG_INF("eDRX parameter update: eDRX: %d, PTW: %d",
					evt->edrx_cfg.edrx, evt->edrx_cfg.ptw);
			
			if (evt->edrx_cfg.edrx == -1)
			{
				LOG_ERR("Network rejected eDRX request");
			}
			break;

		default:
			break;
	}
}

static int modem_configure(void)
{
	int err;

	LOG_INF("Initializing modem library");

	err = nrf_modem_lib_init();
	if (err) {
		LOG_ERR("Failed to initialize the modem library, error: %d", err);
		return err;
	}

	/* STEP 8 - Request PSM and eDRX from the network */
	err = lte_lc_psm_req(true);
	if (err < 0)
	{
		LOG_ERR("Failed to request PSM from the network, error: %d", err);
		return err;
	}

	err = lte_lc_edrx_req(true);
	if (err < 0)
	{
		LOG_ERR("Failed to request eDRX from the network, error: %d", err);
	}

	LOG_INF("Connecting to LTE network");

	err = lte_lc_init_and_connect_async(lte_handler);
	if (err) {
		LOG_ERR("Modem could not be configured, error: %d", err);
		return err;
	}

	k_sem_take(&lte_connected, K_FOREVER);
	LOG_INF("Connected to LTE network");
	dk_set_led_on(DK_LED2);

	return 0;
}

static void print_fix_data(struct nrf_modem_gnss_pvt_data_frame * p_pvt_data_frame)
{
	LOG_INF("Time (UTC):	%02d:%02d:%02d",
			p_pvt_data_frame->datetime.year,
			p_pvt_data_frame->datetime.month,
			p_pvt_data_frame->datetime.day);
	LOG_INF("Latitude:		%.4f", p_pvt_data_frame->latitude);
	LOG_INF("Longitude:		%.4f", p_pvt_data_frame->longitude);
	LOG_INF("Altitude:		%.1f m", p_pvt_data_frame->altitude);

	/* STEP 3.2 - Store latitude and longitude in gps_data buffer */
	int err = snprintf(gps_data, MESSAGE_SIZE, "Latitude: %.4f, Longitude: %.4f, Altitude: %.1f m",
					   p_pvt_data_frame->latitude,
					   p_pvt_data_frame->longitude,
					   p_pvt_data_frame->altitude);
	if (err < 0)
	{
		LOG_ERR("Failed to store latitude and longitude in gps_data buffer");
	}
}

static void gnss_event_handler(int event)
{
	int err;

	switch (event)
	{
		case NRF_MODEM_GNSS_EVT_PVT:
			LOG_INF("Searching...");

			/* STEP 15 - Print satellite information */
			int num_satellites = 0;
			for (int i = 0; i < NRF_MODEM_GNSS_MAX_SATELLITES ; i++)
			{
				// Check if the satellite is in view and has a valid signal
				if (pvt_data_frame.sv[i].signal > 0)
				{
					num_satellites++;
					LOG_INF("SV ID: %3d, singal type: %3d, SNR: %3d db/Hz, flags: USED_IN_FIX: %s | UNHEALTHY: %s",
							pvt_data_frame.sv[i].sv,
							pvt_data_frame.sv[i].signal,
							pvt_data_frame.sv[i].cn0/10,
							pvt_data_frame.sv[i].flags & NRF_MODEM_GNSS_SV_FLAG_USED_IN_FIX ? "true " : "false",
							pvt_data_frame.sv[i].flags & NRF_MODEM_GNSS_SV_FLAG_UNHEALTHY ? "true " : "false");
				}
			}
			LOG_INF("Numbers of satellites in view: %d", num_satellites);

			err = nrf_modem_gnss_read(&pvt_data_frame,
									  sizeof(pvt_data_frame),
									  NRF_MODEM_GNSS_DATA_PVT);
			if (err < 0)
			{
				LOG_INF("Failed to read GNSS data, error: %d", err);
				return;
			}

			if (pvt_data_frame.flags & NRF_MODEM_GNSS_PVT_FLAG_FIX_VALID)
			{
				if(!ttff_first_fix)
				{
					/* STEP 12.3 - Log the TTFF */
					dk_set_led_on(DK_LED1);
					LOG_INF("Time to first fix (TTFF): %2.2lld seconds",
							(k_uptime_get() - ttff_start_time) / 1000);
					ttff_first_fix = true;
				}
				LOG_INF("Valid fix");
				print_fix_data(&pvt_data_frame);
				return;
			}
			else
			{
				LOG_INF("No valid fix, error: %d", pvt_data_frame.flags);
			}

			/* STEP 5 - Check for the flags indicating GNSS is blocked */
			if (pvt_data_frame.flags & NRF_MODEM_GNSS_PVT_FLAG_DEADLINE_MISSED) {
				LOG_INF("GNSS is blocked and missed the deadline due to LTE activity");
			}

			if (pvt_data_frame.flags & NRF_MODEM_GNSS_PVT_FLAG_NOT_ENOUGH_WINDOW_TIME) {
				LOG_INF("GNSS received too short window time to get a fix");
			}
			break;

		case NRF_MODEM_GNSS_EVT_PERIODIC_WAKEUP:
			LOG_INF("GNSS wakeup in periodic mode");
			break;
		
		case NRF_MODEM_GNSS_EVT_SLEEP_AFTER_FIX:
			LOG_INF("GNSS sleep after fix in periodic mode");
			break;

		default:
			break;
	}
	/* Empty log for readability */
	LOG_INF("");
}

static int gnss_init_and_start(void)
{
	/* STEP 4 - Set the modem mode to normal */
	if (lte_lc_func_mode_set(LTE_LC_FUNC_MODE_NORMAL) < 0)
	{
		LOG_ERR("Failed to set the modem to fully functional mode (LTE+GNSS)");
		return -1;
	}

	if (nrf_modem_gnss_event_handler_set(gnss_event_handler) != 0) {
		LOG_ERR("Failed to set GNSS event handler");
		return -1;
	}

	if (nrf_modem_gnss_fix_interval_set(CONFIG_GNSS_PERIODIC_INTERVAL) != 0) {
		LOG_ERR("Failed to set GNSS fix interval");
		return -1;
	}

	if (nrf_modem_gnss_fix_retry_set(CONFIG_GNSS_PERIODIC_TIMEOUT) != 0) {
		LOG_ERR("Failed to set GNSS fix retry");
		return -1;
	}

	LOG_INF("Starting GNSS");
	if (nrf_modem_gnss_start() != 0) {
		LOG_ERR("Failed to start GNSS");
		return -1;
	}

	/* Enabling GNSS over LTE idle mode procedues is required if setting PSM or eDRX
	 * does not work (if it works enabling priority can be commented out).
	 * Priority will be disabled after the first fix is obtained. */
	int err = nrf_modem_gnss_prio_mode_enable();
	if (err != 0) {
		LOG_ERR("Failed to set priority mode for GNSS, error: %d", err);
	}
	else {
		LOG_INF("GNSS priority mode set");
	}

	ttff_start_time = k_uptime_get();

	return 0;
}

static void button_handler(uint32_t button_state, uint32_t has_changed)
{
	int err;
	/* STEP 3.3 - Upon button 1 push, send gps_data */
	switch (has_changed)
	{
	case DK_BTN1_MSK:
		err = send(sock, &gps_data, sizeof(gps_data), 0);
		if (err < 0) {
			LOG_ERR("Failed to send data to server: %d", errno);
		}
		break;

	default:
		break;
	}
}

int main(void)
{
	int err;
	int received;

	if (dk_leds_init() != 0) {
		LOG_ERR("Failed to initialize the LED library");
	}

	err = modem_configure();
	if (err) {
		LOG_ERR("Failed to configure the modem");
		return 0;
	}

	if (dk_buttons_init(button_handler) != 0) {
		LOG_ERR("Failed to initialize the buttons library");
	}

	if (server_resolve() != 0) {
		LOG_INF("Failed to resolve server name");
		return 0;
	}

	if (server_connect() != 0) {
		LOG_INF("Failed to initialize client");
		return 0;
	}

	if (gnss_init_and_start() != 0) {
		LOG_ERR("Failed to initialize and start GNSS");
		return 0;
	}

	while (1) {
		received = recv(sock, recv_buf, sizeof(recv_buf) - 1, 0);

		if (received < 0) {
			LOG_ERR("Socket error: %d, exit", errno);
			break;
		} else if (received == 0) {
			break;
		}

		recv_buf[received] = 0;
		LOG_INF("Data received from the server: (%s)", recv_buf);

	}

	(void)close(sock);

	return 0;
}
