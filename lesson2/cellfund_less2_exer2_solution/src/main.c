/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <stdio.h>
#include <dk_buttons_and_leds.h>
#include <logging/log.h>
/* STEP 4 - Include the header file of the LTE link controller library */
#include <modem/lte_lc.h>

LOG_MODULE_REGISTER(Lesson2_Exercise1, LOG_LEVEL_INF);
#define REGISTERED_STATUS_LED          DK_LED2

/* STEP 5 - Define the semaphore lte_connected */
static K_SEM_DEFINE(lte_connected, 0, 1);

/* STEP 7 - Define the callback function lte_handler()*/ 
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
	default:
		break;
	}
}

/* STEP 6 - Define the function modem_configure() to initialize an LTE connection */
static void modem_configure(void)
{
	int err = lte_lc_init_and_connect_async(lte_handler);
	if (err) {
		LOG_ERR("Modem could not be configured, error: %d", err);
		return;
	}
}

void main(void)
{
	int ret = dk_leds_init();
	if (ret){
		LOG_ERR("Failed to initlize the LEDs Library");
	}
	/* STEP 8 - Call modem_configure() to initiate the LTE connection */
	modem_configure();

	LOG_INF("Connecting to LTE network, this may take several minutes...");

	/* STEP 9 - Take the semaphore lte_connected */
	k_sem_take(&lte_connected, K_FOREVER);
	
	//We only reach here if we are registered to a network
	LOG_INF("Connected to LTE network");
	dk_set_led_on(REGISTERED_STATUS_LED);
}