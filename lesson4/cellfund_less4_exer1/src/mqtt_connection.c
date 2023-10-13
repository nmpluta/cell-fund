#include <stdio.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/random/rand32.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/mqtt.h>
#include <nrf_modem_at.h>
#include <zephyr/logging/log.h>
#include <dk_buttons_and_leds.h>
#include "mqtt_connection.h"

/* Buffers for MQTT client. */
static uint8_t rx_buffer[CONFIG_MQTT_MESSAGE_BUFFER_SIZE];
static uint8_t tx_buffer[CONFIG_MQTT_MESSAGE_BUFFER_SIZE];
static uint8_t payload_buf[CONFIG_MQTT_PAYLOAD_BUFFER_SIZE];

/* MQTT Broker details. */
static struct sockaddr_storage broker;

LOG_MODULE_DECLARE(Lesson4_Exercise1);

/**@brief Function to get the payload of recived data.
 */
static int get_received_payload(struct mqtt_client *c, size_t length)
{
	int ret;
	int err = 0;

	/* Clear the payload buffer. */
	memset(payload_buf, 0x0, sizeof(payload_buf));

	/* Return an error if the payload is larger than the payload buffer.
	 * Note: To allow new messages, we have to read the payload before returning.
	 */
	if (length > sizeof(payload_buf)) {
		err = -EMSGSIZE;
	}

	/* Truncate payload until it fits in the payload buffer. */
	while (length > sizeof(payload_buf)) {
		ret = mqtt_read_publish_payload_blocking(
				c, payload_buf, (length - sizeof(payload_buf)));
		if (ret == 0) {
			return -EIO;
		} else if (ret < 0) {
			return ret;
		}

		length -= ret;
	}

	ret = mqtt_readall_publish_payload(c, payload_buf, length);
	if (ret) {
		return ret;
	}

	return err;
}

/**@brief Function to subscribe to the configured topic
 */
/* STEP 4 - Define the function subscribe() to subscribe to a specific topic.  */
int subscribe(struct mqtt_client * const p_client)
{
	int err;

	/* Define subscribed topic. */
	struct mqtt_topic topic = {
		.topic = {
			.utf8 = CONFIG_MQTT_SUB_TOPIC,
			.size = strlen(CONFIG_MQTT_SUB_TOPIC)
		},
		.qos = MQTT_QOS_1_AT_LEAST_ONCE,
	};

	/* Subsription list. */
	const struct mqtt_subscription_list subscription_list = {
		.list = &topic,
		.list_count = 1,
		.message_id = sys_rand32_get()
	};

	/* Subscribe to the topic. */
	err = mqtt_subscribe(p_client, &subscription_list);
	if (err)
	{
		LOG_ERR("Failed to subscribe to topic, error: %d", err);
		return err;
	}
	else
	{
		LOG_INF("Subscribed to topic: %s", CONFIG_MQTT_SUB_TOPIC);
	}

	return err;
}


/**@brief Function to print strings without null-termination
 */
static void data_print(uint8_t *prefix, uint8_t *data, size_t len)
{
	char buf[len + 1];

	memcpy(buf, data, len);
	buf[len] = 0;
	LOG_INF("%s%s", (char *)prefix, (char *)buf);
}

/**@brief Function to publish data on the configured topic
 */
/* STEP 7.1 - Define the function data_publish() to publish data */
int data_publish(struct mqtt_client * p_client, enum mqtt_qos qos, uint8_t * p_data, size_t len)
{
	int err;

	struct mqtt_publish_param param;
	/* Define MQTT message parameters. */
	param.message.topic.qos = qos;
	param.message.topic.topic.utf8 = CONFIG_MQTT_PUB_TOPIC;
	param.message.topic.topic.size = strlen(CONFIG_MQTT_PUB_TOPIC);
	param.message.payload.data = p_data;
	param.message.payload.len = len;
	param.message_id = sys_rand32_get();
	param.dup_flag = 0;
	param.retain_flag = 0;

	/* Publish MQTT message. */
	LOG_INF("Publishing: %s to topic: %s", p_data, CONFIG_MQTT_PUB_TOPIC);
	err = mqtt_publish(p_client, &param);
	if (err != 0)
	{
		LOG_ERR("Failed to publish message, error: %d", err);
	}
	return err;
}



/**@brief MQTT client event handler
 */
void mqtt_evt_handler(struct mqtt_client *const c,
					  const struct mqtt_evt *evt)
{
	int err;

	switch (evt->type) {
	case MQTT_EVT_CONNACK:
	{
		LOG_INF("MQTT client connected!");
		/* STEP 5 - Subscribe to the topic CONFIG_MQTT_SUB_TOPIC when we have a successful connection */
		err = subscribe(c);
		if (err)
		{
			LOG_ERR("Failed to subscribe to topic, error: %d", err);
		}
		break;
	}

	case MQTT_EVT_DISCONNECT:
		LOG_INF("MQTT client disconnected: %d", evt->result);
		break;

	case MQTT_EVT_PUBLISH:
	/* STEP 6 - Listen to published messages received from the broker and extract the message */
	{
		/* STEP 6.1 - Extract the payload */
		err = get_received_payload(c, evt->param.publish.message.payload.len);
		if (err) {
			LOG_ERR("Failed to extract payload, error: %d", err);
			break;
		}

		/* Send acknowledgement to the broker, if QoS1 publish message is received. */
		if (evt->param.publish.message.topic.qos == MQTT_QOS_1_AT_LEAST_ONCE) 
		{
			const struct mqtt_puback_param ack = {
				.message_id = evt->param.publish.message_id
			};

			/* Send acknowledgement. */
			if (mqtt_publish_qos1_ack(c, &ack) != 0) {
				LOG_ERR("Failed to send MQTT ACK, error: %d", err);
			}
		}

		/* STEP 6.2 - On successful extraction of data */
		if (err >= 0) 
		{
			data_print("Received: ", payload_buf, evt->param.publish.message.payload.len);
		
			if (strncmp((char *)payload_buf,
						CONFIG_TURN_LED_ON_CMD,
						sizeof(CONFIG_TURN_LED_ON_CMD) - 1) == 0)
			{
				dk_set_led_on(LED_CONTROL_OVER_MQTT);
			}
			else if (strncmp((char *)payload_buf,
							CONFIG_TURN_LED_OFF_CMD,
							sizeof(CONFIG_TURN_LED_OFF_CMD) - 1) == 0)
			{
				dk_set_led_off(LED_CONTROL_OVER_MQTT);
			}
			else
			{
				LOG_ERR("Unknown command: %s", (char *)payload_buf);
			}
		}
		/* STEP 6.3 - On failed extraction of data */
		else if (err == -EMSGSIZE) 
		{
			LOG_ERR("Payload too large: %d bytes", evt->param.publish.message.payload.len);
			LOG_INF("Maximum payload size: %d bytes", CONFIG_MQTT_PAYLOAD_BUFFER_SIZE);
		}
		else 
		{
			LOG_ERR("Failed to extract payload, error: %d", err);

			/* Disconnect the client if we failed to extract the payload. */
			err = mqtt_disconnect(c);
			if (err)
			{
				LOG_ERR("Failed to disconnect MQTT client, error: %d", err);
			}
		}
		break;
	}

	case MQTT_EVT_PUBACK:
		if (evt->result != 0) {
			LOG_ERR("MQTT PUBACK error: %d", evt->result);
			break;
		}

		LOG_INF("PUBACK packet id: %u", evt->param.puback.message_id);
		break;

	case MQTT_EVT_SUBACK:
		if (evt->result != 0) {
			LOG_ERR("MQTT SUBACK error: %d", evt->result);
			break;
		}

		LOG_INF("SUBACK packet id: %u", evt->param.suback.message_id);
		break;

	case MQTT_EVT_PINGRESP:
		if (evt->result != 0) {
			LOG_ERR("MQTT PINGRESP error: %d", evt->result);
		}
		break;

	default:
		LOG_INF("Unhandled MQTT event type: %d", evt->type);
		break;
	}
}

/**@brief Resolves the configured hostname and
 * initializes the MQTT broker structure
 */
static int broker_init(void)
{
	int err;
	struct addrinfo *result;
	struct addrinfo *addr;
	struct addrinfo hints = {
		.ai_family = AF_INET,
		.ai_socktype = SOCK_STREAM
	};

	err = getaddrinfo(CONFIG_MQTT_BROKER_HOSTNAME, NULL, &hints, &result);
	if (err) {
		LOG_ERR("getaddrinfo failed: %d", err);
		return -ECHILD;
	}

	addr = result;

	/* Look for address of the broker. */
	while (addr != NULL) {
		/* IPv4 Address. */
		if (addr->ai_addrlen == sizeof(struct sockaddr_in)) {
			struct sockaddr_in *broker4 =
				((struct sockaddr_in *)&broker);
			char ipv4_addr[NET_IPV4_ADDR_LEN];

			broker4->sin_addr.s_addr =
				((struct sockaddr_in *)addr->ai_addr)
				->sin_addr.s_addr;
			broker4->sin_family = AF_INET;
			broker4->sin_port = htons(CONFIG_MQTT_BROKER_PORT);

			inet_ntop(AF_INET, &broker4->sin_addr.s_addr,
				  ipv4_addr, sizeof(ipv4_addr));
			LOG_INF("IPv4 Address found %s", (char *)(ipv4_addr));

			break;
		} else {
			LOG_ERR("ai_addrlen = %u should be %u or %u",
				(unsigned int)addr->ai_addrlen,
				(unsigned int)sizeof(struct sockaddr_in),
				(unsigned int)sizeof(struct sockaddr_in6));
		}

		addr = addr->ai_next;
	}

	/* Free the address. */
	freeaddrinfo(result);

	return err;
}

/* Function to get the client id */
static const uint8_t* client_id_get(void)
{
	static uint8_t client_id[MAX(sizeof(CONFIG_MQTT_CLIENT_ID),
				     CLIENT_ID_LEN)];

	if (strlen(CONFIG_MQTT_CLIENT_ID) > 0) {
		snprintf(client_id, sizeof(client_id), "%s",
			 CONFIG_MQTT_CLIENT_ID);
		goto exit;
	}

	char imei_buf[CGSN_RESPONSE_LENGTH + 1];
	int err;

	err = nrf_modem_at_cmd(imei_buf, sizeof(imei_buf), "AT+CGSN");
	if (err) {
		LOG_ERR("Failed to obtain IMEI, error: %d", err);
		goto exit;
	}

	imei_buf[IMEI_LEN] = '\0';

	snprintf(client_id, sizeof(client_id), "nrf-%.*s", IMEI_LEN, imei_buf);

exit:
	LOG_DBG("client_id = %s", (char *)(client_id));

	return client_id;
}


/**@brief Initialize the MQTT client structure
 */
/* STEP 3 - Define the function client_init() to initialize the MQTT client instance.  */
int client_init(struct mqtt_client * p_client)
{
	int err;

	mqtt_client_init(p_client);

	/* Broker details. */
	err = broker_init();
	if (err) {
		LOG_ERR("Failed to initialize broker connection");
		return err;
	}

	/* MQTT client configuration */
	p_client->broker = &broker;
	p_client->evt_cb = mqtt_evt_handler;
	p_client->client_id.utf8 = client_id_get();
	p_client->client_id.size = strlen(client_id_get());
	p_client->password = NULL;
	p_client->user_name = NULL;
	p_client->protocol_version = MQTT_VERSION_3_1_1;

	/* MQTT buffers configuration */
	p_client->rx_buf = rx_buffer;
	p_client->rx_buf_size = sizeof(rx_buffer);
	p_client->tx_buf = tx_buffer;
	p_client->tx_buf_size = sizeof(tx_buffer);

	/* Disable TLS. */
	p_client->transport.type = MQTT_TRANSPORT_NON_SECURE;

	return err;
}

/**@brief Initialize the file descriptor structure used by poll.
 */
int fds_init(struct mqtt_client *c, struct pollfd *fds)
{
	if (c->transport.type == MQTT_TRANSPORT_NON_SECURE) {
		fds->fd = c->transport.tcp.sock;
	} else {
		return -ENOTSUP;
	}

	fds->events = POLLIN;

	return 0;
}