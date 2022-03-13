/*
 * Copyright (c) 2017 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __CONFIG_H__
#define __CONFIG_H__

// #define ZEPHYR_ADDR		"192.168.1.65"
// #define SERVER_ADDR		"37.187.106.16" //test.mosquitto.org

#define SERVER_HOSTNAME         "awvvdv9pe6z46-ats.iot.ap-southeast-1.amazonaws.com"
// #define SERVER_HOSTNAME         "test.mosquitto.org"
#define SERVER_PORT                     8883

#define APP_CONNECT_TIMEOUT_MS	2000
#define APP_SLEEP_MSECS		500

#define APP_CONNECT_TRIES	3

#define MQTT_CLIENTID		"zephyr_publisher"

#endif
