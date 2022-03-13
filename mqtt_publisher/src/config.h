#ifndef __CONFIG_H__
#define __CONFIG_H__

#define SERVER_HOSTNAME		"broker.hivemq.com" /*MQTT Hostname*/
#define SERVER_ADDR		"192.168.1.10"

#define SERVER_PORT             1883

#define APP_CONNECT_TIMEOUT_MS	2000
#define APP_SLEEP_MSECS		500

#define APP_CONNECT_TRIES	3

#define APP_MQTT_BUFFER_SIZE	128

#define MQTT_CLIENTID		"zephyr_publisher"

#endif
