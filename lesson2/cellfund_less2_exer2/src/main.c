/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <dk_buttons_and_leds.h>

/* STEP 4 - Include the header file of the nRF Modem library and the LTE link controller library */
#include <modem/nrf_modem_lib.h>
#include <modem/lte_lc.h>

/* STEP 5 - Define the semaphore lte_connected */
static K_SEM_DEFINE(lte_connected, 0, 1);

LOG_MODULE_REGISTER(Lesson2_Exercise2, LOG_LEVEL_INF);

/* STEP 7 - Define the event handler for LTE link control */
static void lte_handler(const struct lte_lc_evt *const evt)
{
	switch (evt->type) 
	{
		/* STEP 7.1 - On changed registration status, print status */
		case LTE_LC_EVT_NW_REG_STATUS:
		{
			if (evt->nw_reg_status == LTE_LC_NW_REG_REGISTERED_HOME ||
				evt->nw_reg_status == LTE_LC_NW_REG_REGISTERED_ROAMING) 
			{
				LOG_INF("Network registration status: %s",
					evt->nw_reg_status == LTE_LC_NW_REG_REGISTERED_HOME ?
					"Connected - home network" : "Connected - roaming");
				k_sem_give(&lte_connected);
			} 
			else 
			{
				LOG_INF("Network registration status: %s",
					evt->nw_reg_status == LTE_LC_NW_REG_SEARCHING ?
					"Connecting..." : "Disconnected");
			}
			break;
		}

	/* STEP 7.2 - On event RRC update, print RRC mode */
	case LTE_LC_EVT_RRC_UPDATE:
	{
		LOG_INF("RRC mode: %s", evt->rrc_mode == LTE_LC_RRC_MODE_IDLE ? "Idle" : "Connected");
		break;
	}

	default:
		break;
	}
}

/* STEP 6 - Define the function modem_configure() to initialize an LTE connection */
int modem_configure()
{
	int err;
	err = nrf_modem_lib_init();

	if (err == 0)
	{
		LOG_INF("Modem library initialized");
	}
	else if (err > 0) {
		LOG_INF("Modem firmaware update is performed");
	}
	else
	{
		LOG_ERR("Failed to initialize modem library, error: %d", err);
		return err;
	}

	err = lte_lc_init_and_connect_async(lte_handler);
	if (err != 0)
	{
		LOG_ERR("Failed to intialized LTE modem, error: %d", err);
	}

	return err;
}

int main(void)
{
	if (dk_leds_init() != 0) {
		LOG_ERR("Failed to initialize the LEDs Library");
	}

	/* STEP 8 - Call modem_configure() to initiate the LTE connection */
	if (modem_configure() != 0) {
		LOG_ERR("Failed to initialize the modem");
	}

	LOG_INF("Connecting to LTE network");

	/* STEP 9 - Take the semaphore lte_connected */
	k_sem_take(&lte_connected, K_FOREVER);

	LOG_INF("Connected to LTE network");

	/* STEP 10 - Turn on the LED status LED */
	if (dk_set_led_on(DK_LED1) != 0)
	{
		LOG_ERR("Failed to turn on LED 1");
	}
	else
	{
		LOG_INF("LED 1 is turned on");
	}

	return 0;
}
