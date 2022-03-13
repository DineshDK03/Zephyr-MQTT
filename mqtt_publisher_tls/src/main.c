/*
 * Copyright (c) 2017 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <logging/log.h>
LOG_MODULE_REGISTER(mqtt_publisher_sample, LOG_LEVEL_DBG);

#include <zephyr.h>
#include <sys/printk.h>
#include <string.h>
#include <errno.h>

#include <net/net_if.h>
#include <net/net_core.h>
#include <net/net_context.h>
#include <net/net_mgmt.h>
#include <net/socket.h>
#include <net/mqtt.h>
#include <random/rand32.h>

#include "config.h"

/* Buffers for MQTT client. */
static uint8_t rx_buffer[1024];
static uint8_t tx_buffer[1024];

/* The mqtt client struct */
static struct mqtt_client client_ctx;

/* MQTT Broker details. */
static struct sockaddr_storage broker;

static struct zsock_pollfd fds[1];
static int nfds;

static bool connected;

#if defined(CONFIG_DNS_RESOLVER)
static struct zsock_addrinfo hints;
static struct zsock_addrinfo *haddr;
#endif

static K_SEM_DEFINE(iface_setup, 0, 1);

#if defined(CONFIG_TLS_CREDENTIAL_FILENAMES)
static const unsigned char ca_certificate[] = "ca.der";
static const unsigned char server_certificate[] = "device.der";
static const unsigned char private_key[] = "device.key";
#else
static const unsigned char ca_certificate[] = {
#include "ca.der.inc"
};
static const unsigned char server_certificate[] = {
#include "device.der.inc"
};
static const unsigned char private_key[] = {
#include "device_key.der.inc"
};
#endif

#define APP_CA_CERT_TAG 1

static sec_tag_t m_sec_tags[] = {
		APP_CA_CERT_TAG,
};

static int tls_init(void)
{
	int err = -EINVAL;

	err = tls_credential_add(APP_CA_CERT_TAG, TLS_CREDENTIAL_CA_CERTIFICATE,
			 ca_certificate, sizeof(ca_certificate));
	if (err != 0) {
		LOG_ERR("Failed to register ca certificate: %d", err);
		return err;
	}

	err = tls_credential_add(APP_CA_CERT_TAG,
				 TLS_CREDENTIAL_SERVER_CERTIFICATE,
				 server_certificate,
				 sizeof(server_certificate));
	if (err < 0) {
		LOG_ERR("Failed to register server certificate: %d", err);
	}


	err = tls_credential_add(APP_CA_CERT_TAG,
				 TLS_CREDENTIAL_PRIVATE_KEY,
				 private_key, sizeof(private_key));
	if (err < 0) {
		LOG_ERR("Failed to register private key: %d", err);
	}

	return err;
}

static void prepare_fds(struct mqtt_client *client)
{
	if (client->transport.type == MQTT_TRANSPORT_SECURE) {
		fds[0].fd = client->transport.tls.sock;
	}

	fds[0].events = ZSOCK_POLLIN;
	nfds = 1;
}

static void clear_fds(void)
{
	nfds = 0;
}

static int wait(int timeout)
{
	int ret = 0;

	if (nfds > 0) {
		ret = zsock_poll(fds, nfds, timeout);
		if (ret < 0) {
			LOG_ERR("poll error: %d", errno);
		}
	}

	return ret;
}

void mqtt_evt_handler(struct mqtt_client *const client,
		      const struct mqtt_evt *evt)
{
	int err;

	switch (evt->type) {
	case MQTT_EVT_CONNACK:
		if (evt->result != 0) {
			LOG_ERR("MQTT connect failed %d", evt->result);
			break;
		}

		connected = true;
		LOG_INF("MQTT client connected!");

		break;

	case MQTT_EVT_DISCONNECT:
		LOG_INF("MQTT client disconnected %d", evt->result);

		connected = false;
		clear_fds();

		break;

	case MQTT_EVT_PUBACK:
		if (evt->result != 0) {
			LOG_ERR("MQTT PUBACK error %d", evt->result);
			break;
		}

		LOG_INF("PUBACK packet id: %u", evt->param.puback.message_id);

		break;

	case MQTT_EVT_PUBREC:
		if (evt->result != 0) {
			LOG_ERR("MQTT PUBREC error %d", evt->result);
			break;
		}

		LOG_INF("PUBREC packet id: %u", evt->param.pubrec.message_id);

		const struct mqtt_pubrel_param rel_param = {
			.message_id = evt->param.pubrec.message_id
		};

		err = mqtt_publish_qos2_release(client, &rel_param);
		if (err != 0) {
			LOG_ERR("Failed to send MQTT PUBREL: %d", err);
		}

		break;

	case MQTT_EVT_PUBCOMP:
		if (evt->result != 0) {
			LOG_ERR("MQTT PUBCOMP error %d", evt->result);
			break;
		}

		LOG_INF("PUBCOMP packet id: %u",
			evt->param.pubcomp.message_id);

		break;

	case MQTT_EVT_PINGRESP:
		LOG_INF("PINGRESP packet");
		break;

	default:
		break;
	}
}

static int publish(struct mqtt_client *client, enum mqtt_qos qos)
{

	char topic[] = "metacard_esp32";
	char payload[] = "Hello World!";
	struct mqtt_publish_param param;

	param.message.topic.qos = qos;
	param.message.topic.topic.utf8 = (uint8_t *)topic;
	param.message.topic.topic.size = strlen(topic);
	param.message.payload.data = (uint8_t *)payload;
	param.message.payload.len = strlen(payload);
	param.message_id = sys_rand32_get();
	param.dup_flag = 0U;
	param.retain_flag = 0U;

	return mqtt_publish(client, &param);
}

#define RC_STR(rc) ((rc) == 0 ? "OK" : "ERROR")

#define PRINT_RESULT(func, rc) \
	LOG_INF("%s: %d <%s>", (func), rc, RC_STR(rc))


static void broker_init(void)
{
	struct sockaddr_in *broker4 = (struct sockaddr_in *)&broker;

	broker4->sin_family = AF_INET;
	broker4->sin_port = htons(SERVER_PORT);

#if defined(CONFIG_DNS_RESOLVER)
	net_ipaddr_copy(&broker4->sin_addr,
			&net_sin(haddr->ai_addr)->sin_addr);
#else
	zsock_inet_pton(AF_INET, SERVER_ADDR, &broker4->sin_addr);
#endif

}

static void client_init(struct mqtt_client *client)
{
	mqtt_client_init(client);
	broker_init();

	/* MQTT client configuration */
	client->broker = &broker;
	client->evt_cb = mqtt_evt_handler;
	client->client_id.utf8 = (uint8_t *)MQTT_CLIENTID;
	client->client_id.size = strlen(MQTT_CLIENTID);
	client->password = NULL;
	client->user_name = NULL;
	client->protocol_version = MQTT_VERSION_3_1_1;

	/* MQTT buffers configuration */
	client->rx_buf = rx_buffer;
	client->rx_buf_size = sizeof(rx_buffer);
	client->tx_buf = tx_buffer;
	client->tx_buf_size = sizeof(tx_buffer);

	/* MQTT transport configuration */
	client->transport.type = MQTT_TRANSPORT_SECURE;

	struct mqtt_sec_config *tls_config = &client->transport.tls.config;

	tls_config->peer_verify = TLS_PEER_VERIFY_NONE;
	tls_config->cipher_list = NULL;
	tls_config->sec_tag_list = m_sec_tags;
	tls_config->sec_tag_count = ARRAY_SIZE(m_sec_tags);
	tls_config->hostname = SERVER_HOSTNAME;
	tls_config->cert_nocopy = TLS_CERT_NOCOPY_OPTIONAL;
}

#define SUCCESS_OR_EXIT(rc) { if (rc != 0) { return 1; } }
#define SUCCESS_OR_BREAK(rc) { if (rc != 0) { break; } }

/* In this routine we block until the connected variable is 1 */
static int try_to_connect(struct mqtt_client *client)
{
	int rc, i = 0;

	while (i++ < APP_CONNECT_TRIES && !connected) {

		client_init(client);

		rc = mqtt_connect(client);
		if (rc != 0) {
			PRINT_RESULT("mqtt_connect", rc);
			k_sleep(K_MSEC(APP_SLEEP_MSECS));
			continue;
		}

		prepare_fds(client);

		if (wait(APP_CONNECT_TIMEOUT_MS)) {
			mqtt_input(client);
		}

		if (!connected) {
			mqtt_abort(client);
		}
	}

	if (connected) {
		return 0;
	}

	return -EINVAL;
}

static int process_mqtt_and_sleep(struct mqtt_client *client, int timeout)
{
	int64_t remaining = timeout;
	int64_t start_time = k_uptime_get();
	int rc;

	while (remaining > 0 && connected) {
		if (wait(remaining)) {
			rc = mqtt_input(client);
			if (rc != 0) {
				PRINT_RESULT("mqtt_input", rc);
				return rc;
			}
		}

		rc = mqtt_live(client);
		if (rc != 0 && rc != -EAGAIN) {
			PRINT_RESULT("mqtt_live", rc);
			return rc;
		} else if (rc == 0) {
			rc = mqtt_input(client);
			if (rc != 0) {
				PRINT_RESULT("mqtt_input", rc);
				return rc;
			}
		}

		remaining = timeout + start_time - k_uptime_get();
	}

	return 0;
}

#if defined(CONFIG_NET_DHCPV4)
static struct net_mgmt_event_callback dhcp_cb;

static void handler_cb(struct net_mgmt_event_callback *cb,
		    uint32_t mgmt_event, struct net_if *iface)
{
	if (mgmt_event != NET_EVENT_IPV4_DHCP_BOUND) {
		return;
	}

	char buf[NET_IPV4_ADDR_LEN];

	LOG_INF("Your address: %s",
		log_strdup(net_addr_ntop(AF_INET,
				   &iface->config.dhcpv4.requested_ip,
				   buf, sizeof(buf))));
	LOG_INF("Lease time: %u seconds",
			iface->config.dhcpv4.lease_time);
	LOG_INF("Subnet: %s",
		log_strdup(net_addr_ntop(AF_INET,
					&iface->config.ip.ipv4->netmask,
					buf, sizeof(buf))));
	LOG_INF("Router: %s",
		log_strdup(net_addr_ntop(AF_INET,
						&iface->config.ip.ipv4->gw,
						buf, sizeof(buf))));
	k_sem_give(&iface_setup);
}

static void setup_dhcpv4(struct net_if *iface)
{
	LOG_INF("Running dhcpv4 client...");

	net_mgmt_init_event_callback(&dhcp_cb, handler_cb,
				     NET_EVENT_IPV4_DHCP_BOUND);

	net_mgmt_add_event_callback(&dhcp_cb);

	net_dhcpv4_start(iface);
}
#endif

#if defined(CONFIG_DNS_RESOLVER)
static int get_mqtt_broker_addrinfo(void)
{
	int retries = 3;
	int rc = -EINVAL;

	while (retries--) {
		hints.ai_family = AF_INET;
		hints.ai_socktype = SOCK_STREAM;
		hints.ai_protocol = 0;

		rc = zsock_getaddrinfo(SERVER_HOSTNAME, "8883",
				       &hints, &haddr);
		if (rc == 0) {
			LOG_INF("DNS resolved for %s:%d", SERVER_HOSTNAME, SERVER_PORT);
			return 0;
		}

		LOG_ERR("DNS not resolved for %s:%d, retrying", SERVER_HOSTNAME, SERVER_PORT);
	}

	return rc;
}
#endif

int main(void)
{
	int r = 0,rc;
	struct net_if *iface;

	iface = net_if_get_default();
	if (!iface) {
		printk("wifi interface not available");
		return -1;
	}

#if defined(CONFIG_NET_DHCPV4)
	setup_dhcpv4(iface);
	k_sem_take(&iface_setup, K_FOREVER);
#endif

#if defined(CONFIG_DNS_RESOLVER)
		rc = get_mqtt_broker_addrinfo();
		PRINT_RESULT("get_mqtt_broker_addrinfo", rc);
#endif
	rc = tls_init();
	PRINT_RESULT("tls_init", rc);

	LOG_INF("attempting to connect: ");
	rc = try_to_connect(&client_ctx);
	PRINT_RESULT("try_to_connect", rc);

	while (connected) {
		r = mqtt_ping(&client_ctx);
		PRINT_RESULT("mqtt_ping", r);
		SUCCESS_OR_BREAK(r);

		r = process_mqtt_and_sleep(&client_ctx, APP_SLEEP_MSECS);
		SUCCESS_OR_BREAK(r);

		r = publish(&client_ctx, MQTT_QOS_0_AT_MOST_ONCE);
		PRINT_RESULT("mqtt_publish", r);
		SUCCESS_OR_BREAK(r);
	}

	r = mqtt_disconnect(&client_ctx);
	PRINT_RESULT("mqtt_disconnect", r);

	LOG_INF("Bye!");

	return 0;
}
