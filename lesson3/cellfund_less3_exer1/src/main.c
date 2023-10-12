/*
 * Copyright (c) 2022 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <dk_buttons_and_leds.h>
#include <modem/nrf_modem_lib.h>
#include <modem/lte_lc.h>

/* STEP 3 - Include the header file for the socket API */
#include <zephyr/net/socket.h>

/* STEP 4 - Define the hostname and port for the echo server */
#define SERVER_HOSTNAME "nordicecho.westeurope.cloudapp.azure.com"
#define SERVER_PORT "2444"

#define MESSAGE_SIZE 256
#define MESSAGE_TO_SEND "Hello from nRF9160 SiP"
#define SSTRLEN(s) (sizeof(s) - 1)

/* STEP 5.1 - Declare the structure for the socket and server address */
static int sock;
struct sockaddr_storage server;


/* STEP 5.2 - Declare the buffer for receiving from server */
static char rx_buffer[MESSAGE_SIZE];

K_SEM_DEFINE(lte_connected, 0, 1);

LOG_MODULE_REGISTER(Lesson3_Exercise1, LOG_LEVEL_INF);

static int server_resolve(void)
{
	int err;

	/* STEP 6.1 - Call getaddrinfo() to get the IP address of the echo server */
	struct addrinfo *result;
	struct addrinfo hints = 
	{
		.ai_family = AF_INET,
		.ai_socktype = SOCK_DGRAM,
	};
	
	err = getaddrinfo(SERVER_HOSTNAME, SERVER_PORT, &hints, &result);
	if (err != 0) 
	{
		LOG_ERR("getaddrinfo failed: %d\n", err);
		return -1;
	}

	if (result == NULL) 
	{
		LOG_ERR("getaddrinfo failed: Address not found\n");
		return -1;
	}

	/* STEP 6.2 - Retrieve the relevant information from the result structure */
	struct sockaddr_in * p_server4 = (struct sockaddr_in *)&server;
	p_server4->sin_family = hints.ai_family;
	p_server4->sin_addr.s_addr = ((struct sockaddr_in *)result->ai_addr)->sin_addr.s_addr;
	p_server4->sin_port = ((struct sockaddr_in *)result->ai_addr)->sin_port;

	/* STEP 6.3 - Convert the address into a string and print it */
	char ipv4_addr[NET_IPV4_ADDR_LEN];
	inet_ntop(p_server4->sin_family, &p_server4->sin_addr, ipv4_addr, sizeof(ipv4_addr));

	/* Print server_4 information. */
	LOG_INF("Server family: %d", p_server4->sin_family);
	LOG_INF("Server address: %s", ipv4_addr);
	LOG_INF("Server port: %d", ntohs(p_server4->sin_port));

	/* STEP 6.4 - Free the memory allocated for result */
	freeaddrinfo(result);

	return 0;
}

static int server_connect(void)
{
	int err;
	/* STEP 7 - Create a UDP socket */
	static int sock;
	sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (sock < 0) 
	{
		LOG_ERR("Failed to create UDP socket: %d", errno);
		return -1;
	}

	/* STEP 8 - Connect the socket to the server */
	err = connect(sock, (struct sockaddr *) &server, NET_SOCKADDR_MAX_SIZE);

	LOG_INF("Successfully connected to server");

	return 0;
}

static void lte_handler(const struct lte_lc_evt *const evt)
{
     switch (evt->type) {
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
		LOG_INF("RRC mode: %s", evt->rrc_mode == LTE_LC_RRC_MODE_CONNECTED ?
				"Connected" : "Idle");
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

	LOG_INF("Connecting to LTE network");

	err = lte_lc_init_and_connect_async(lte_handler);
	if (err) {
		LOG_INF("Modem could not be configured, error: %d", err);
		return err;
	}

	k_sem_take(&lte_connected, K_FOREVER);
	LOG_INF("Connected to LTE network");
	dk_set_led_on(DK_LED2);

	return 0;
}

static void button_handler(uint32_t button_state, uint32_t has_changed)
{
	int res;
	switch (has_changed)
	{
		case DK_BTN1_MSK:
			/* STEP 9 - call send() when button 1 is pressed */
			res = send(sock, MESSAGE_TO_SEND, SSTRLEN(MESSAGE_TO_SEND), 0);
			if (res < 0) 
			{
				LOG_ERR("Failed to send data to server: %d", errno);
			}
			else
			{
				LOG_INF("Successfully sent %d bytes to server", res);
				LOG_INF("Message sent: %s", MESSAGE_TO_SEND);
			}
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

	LOG_INF("Press button 1 on your DK or Thingy:91 to send your message");

	while (1) {
		/* STEP 10 - Call recv() to listen to received messages */
		received = recv(sock, rx_buffer, sizeof(rx_buffer), 0);
		if (received < 0) 
		{
			LOG_ERR("Failed to receive data from server: %d", errno);
		}
		else
		{
			LOG_INF("Received %d bytes from server", received);
			LOG_INF("Received message: %s", rx_buffer);
		}
	}

	(void)close(sock);

	return 0;
}
