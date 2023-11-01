/*
 * Copyright (c) 2022 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <stdio.h>
#include <zephyr/kernel.h>

#include <zephyr/logging/log.h>
#include <modem/nrf_modem_lib.h>
#include <modem/lte_lc.h>
#include <dk_buttons_and_leds.h>

/* STEP 4 - Include the header file for the GNSS interface */
#include <nrf_modem_gnss.h>

/* STEP 5 - Define the PVT data frame variable */
static struct nrf_modem_gnss_pvt_data_frame pvt_data_frame;

/* STEP 12.1 - Declare helper variables to find the TTFF */

/** @brief The variable `ttff_start_time` is used to store the start time 
  * of the Time to First Fix (TTFF) process. TTFF is the time it takes for 
  * a GNSS receiver to acquire satellite signals and calculate a position fix. */
static int64_t ttff_start_time;

/** @brief This variable is used to keep track of whether the first fix 
  * has been obtained during the Time to First Fix (TTFF) process. */
static bool ttff_first_fix = false;

static K_SEM_DEFINE(lte_connected, 0, 1);

LOG_MODULE_REGISTER(Lesson6_Exercise1, LOG_LEVEL_INF);

static int modem_configure(void)
{
	int err;

	LOG_INF("Initializing modem library");

	err = nrf_modem_lib_init();
	if (err) {
		LOG_ERR("Failed to initialize the modem library, error: %d", err);
		return err;
	}

	err = lte_lc_init();
	if (err) {
		LOG_ERR("Failed to initialize LTE Link Controller, error: %d", err);
		return err;
	}

	return 0;
}

/* STEP 6 - Define a function to log fix data in a readable format */
static void print_fix_data(struct nrf_modem_gnss_pvt_data_frame * p_pvt_data_frame)
{
	LOG_INF("Time (UTC):	%02d:%02d:%02d",
			p_pvt_data_frame->datetime.year,
			p_pvt_data_frame->datetime.month,
			p_pvt_data_frame->datetime.day);
	LOG_INF("Latitude:	%.4f", p_pvt_data_frame->latitude);
	LOG_INF("Longitude:	%.4f", p_pvt_data_frame->longitude);
	LOG_INF("Altitude:	%.1f m\n", p_pvt_data_frame->altitude);
}

static void gnss_event_handler(int event)
{
	int err;

	switch (event)
	{
		/* STEP 7.1 - On a PVT event, confirm if PVT data is a valid fix */
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
				LOG_INF("Valid fix\n");
				print_fix_data(&pvt_data_frame);
				return;
			}
			else
			{
				LOG_INF("No valid fix, error: %d\n", pvt_data_frame.flags);
			}
			break;

		/* STEP 7.2 - Log when the GNSS sleeps and wakes up */
		case NRF_MODEM_GNSS_EVT_PERIODIC_WAKEUP:
			LOG_INF("GNSS wakeup in periodic mode");
			break;
		
		case NRF_MODEM_GNSS_EVT_SLEEP_AFTER_FIX:
			LOG_INF("GNSS sleep after fix in periodic mode");
			break;

		default:
			break;
	}
}

int main(void)
{
	int err;

	if (dk_leds_init() != 0) {
		LOG_ERR("Failed to initialize the LEDs Library");
	}

	err = modem_configure();
	if (err) {
		LOG_ERR("Failed to configure the modem");
		return 0;
	}

	/* STEP 8 - Activate only the GNSS stack */
	err = lte_lc_func_mode_set(LTE_LC_FUNC_MODE_ACTIVATE_GNSS);
	if (err < 0)
	{
		LOG_ERR("Failed to activate GNSS functional mode, error: %d", err);
		return 0;
	}

	/* STEP 9 - Register the GNSS event handler */
	err = nrf_modem_gnss_event_handler_set(gnss_event_handler);
	if (err != 0)
	{
		LOG_ERR("Failed to register GNSS event handler, error: %d", err);
		return 0;
	}

	/* STEP 10 - Set the GNSS fix interval and GNSS fix retry period */
	err = nrf_modem_gnss_fix_interval_set(CONFIG_GNSS_PERIODIC_INTERVAL);
	if (err < 0)
	{
		LOG_ERR("Failed to set GNSS fix interval, error: %d", err);
		return 0;
	}

	err = nrf_modem_gnss_fix_retry_set(CONFIG_GNSS_PERIODIC_TIMEOUT);
	if (err < 0)
	{
		LOG_ERR("Failed to set GNSS fix retry period, error: %d", err);
		return 0;
	}

	/* STEP 11 - Start the GNSS receiver*/
	err = nrf_modem_gnss_start();
	LOG_INF("Starting GNSS");
	if (err < 0)
	{
		LOG_ERR("Failed to start GNSS receiver, error: %d", err);
		return 0;
	}

	/* STEP 12.2 - Log the current system uptime */
	ttff_start_time = k_uptime_get();



	return 0;
}
