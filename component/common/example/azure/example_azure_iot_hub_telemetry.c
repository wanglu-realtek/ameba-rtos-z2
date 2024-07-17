#include "example_azure_iot_entry.h"
#ifdef EXAMPLE_AZURE_IOT_HUB_TELEMETRY

// Copyright (c) Microsoft Corporation. All rights reserved.
// SPDX-License-Identifier: MIT

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <azure/az_core.h>
#include <azure/az_iot.h>

#include "FreeRTOS.h"
#include "task.h"

#include "MQTTClient.h"
#include "wifi_conf.h"

#include "example_azure_iot_common.h"

#define SAMPLE_TYPE AZIOT_HUB
#define SAMPLE_NAME AZIOT_HUB_TELEMETRY_SAMPLE

#define HUB_HOSTNAME "[IoT Hub Name].azure-devices.net"	
#define HUB_DEVICE_ID "[Device ID]"

#define X509CERTIFICATE \
"-----BEGIN CERTIFICATE-----""\n" \
"MIICXzCCAgWgAwIBAgIJALmwdKzRWp0FMAoGCCqGSM49BAMCMIGLMQswCQYDVQQG""\n" \
"...""\n" \
"AiEAwIgsujX4+jkGvuJTnr74xpSWUxuJCPBUFmG/OHs4wdM=""\n" \
"-----END CERTIFICATE-----""\n"

#define X509PRIVATEKEY \
"-----BEGIN EC PARAMETERS-----""\n" \
"...""\n" \
"-----END EC PARAMETERS-----""\n" \
"-----BEGIN EC PRIVATE KEY-----""\n" \
"MHcCAQEEINO+QhrBdnUP36taC7VO6i0UNoBrbri8i8m3Jx/qIwkroAoGCCqGSM49""\n" \
"...""\n" \
"dpNJXSmXGMOJ5+6tUdkBuJ1gnxqgxj5rnw==""\n" \
"-----END EC PRIVATE KEY-----""\n"

	
#define TELEMETRY_SEND_INTERVAL_SEC 1
#define TELEMETRY_RETRY_INTERVAL_SEC 1
#define MAX_TELEMETRY_MESSAGE_COUNT 5
#define MQTT_TIMEOUT_DISCONNECT_MS (10 * 1000)

static iot_sample_variables az_vars;
static az_iot_hub_client hub_client;
static MQTTClient mqtt_client;
static Network mqtt_network;
static MQTTPacket_connectData connectData = MQTTPacket_connectData_initializer;
static unsigned char mqtt_sendbuf[1024], mqtt_readbuf[1024];

static void create_and_configure_mqtt_client(void)
{
	int rc;

	import_user_configuration(&az_vars, IOT_SAMPLE_HUB_HOSTNAME, HUB_HOSTNAME);
	import_user_configuration(&az_vars, IOT_SAMPLE_HUB_DEVICE_ID, HUB_DEVICE_ID);
	import_user_configuration(&az_vars, IOT_SAMPLE_DEVICE_X509_CERTIFICATE, X509CERTIFICATE);
	import_user_configuration(&az_vars, IOT_SAMPLE_DEVICE_X509_PRIVATE_KEY, X509PRIVATEKEY);
	
	// Check variables set by user for purposes of running sample.
	iot_sample_check_variables(SAMPLE_TYPE, SAMPLE_NAME, &az_vars);

	// Initialize the hub client with the default connection options.
	rc = az_iot_hub_client_init(&hub_client, az_vars.hub_hostname, az_vars.hub_device_id, NULL);
	if (az_result_failed(rc))
	{
		IOT_SAMPLE_LOG_ERROR("Failed to initialize hub client: az_result return code 0x%08x.", rc);
	}

	NetworkInit(&mqtt_network);
	mqtt_network.clientCA = (char*)az_span_ptr(az_vars.x509_certificate);
	mqtt_network.private_key = (char*)az_span_ptr(az_vars.x509_private_key);
	mqtt_network.use_ssl = 1;

	MQTTClientInit(&mqtt_client, &mqtt_network, 30000, mqtt_sendbuf, sizeof(mqtt_sendbuf), mqtt_readbuf, sizeof(mqtt_readbuf));
  
}

static void connect_mqtt_client_to_iot_hub(void)
{
	int rc;
	char mqtt_client_username_buffer[128];
	char mqtt_client_id_buffer[128];

	// Get the MQTT client username.
	rc = az_iot_hub_client_get_user_name(&hub_client, mqtt_client_username_buffer, sizeof(mqtt_client_username_buffer), NULL);
	if (az_result_failed(rc))
	{
		IOT_SAMPLE_LOG_ERROR("Failed to get MQTT client username: az_result return code 0x%08x.", rc);
	}
	// Get the MQTT client id used for the MQTT connection.
	rc = az_iot_hub_client_get_client_id(&hub_client, mqtt_client_id_buffer, sizeof(mqtt_client_id_buffer), NULL);
	if (az_result_failed(rc))
	{
	  IOT_SAMPLE_LOG_ERROR("Failed to get MQTT client id: az_result return code 0x%08x.", rc);
	}

	// Set MQTT connection options.
	connectData.clientID.cstring = mqtt_client_id_buffer;
	connectData.username.cstring = mqtt_client_username_buffer;
	connectData.cleansession = false;
	connectData.keepAliveInterval = AZ_IOT_DEFAULT_MQTT_CONNECT_KEEPALIVE_SECONDS;

	// Connect to iot hub device
	while (1) {
		do {
			if (mqtt_client.isconnected == 0) {
				  
				if (NetworkConnect(mqtt_client.ipstack, (char*)az_span_ptr(az_vars.hub_hostname), 8883) != 0) {
					break;
				}
				mqtt_printf(MQTT_INFO, "\"%s\" Connected", (char*)az_span_ptr(az_vars.hub_hostname));
  
				if (MQTTConnect(&mqtt_client, &connectData) != 0) {
					break;
				}
				mqtt_printf(MQTT_INFO, "MQTT Connected");
			}
		} while (0);
  
		if (mqtt_client.isconnected) {
			break;
		}
  
    	iot_sample_sleep_for_seconds(TELEMETRY_RETRY_INTERVAL_SEC);
	}

}

static void send_telemetry_messages_to_iot_hub(void)
{
	int rc;

	// Get the Telemetry topic to publish the telemetry messages.
	char telemetry_topic_buffer[128];
	rc = az_iot_hub_client_telemetry_get_publish_topic(&hub_client, NULL, telemetry_topic_buffer, sizeof(telemetry_topic_buffer), NULL);
	if (az_result_failed(rc))
	{
		IOT_SAMPLE_LOG_ERROR("Failed to get the Telemetry topic: az_result return code 0x%08x.", rc);
	}
	char const* telemetry_message_payloads[MAX_TELEMETRY_MESSAGE_COUNT] = {
		"{\"message_number\":1}", "{\"message_number\":2}", "{\"message_number\":3}",
		"{\"message_number\":4}", "{\"message_number\":5}",
	};

	// Publish # of telemetry messages.
	for (uint8_t message_count = 0; message_count < MAX_TELEMETRY_MESSAGE_COUNT; message_count++)
	{
		MQTTMessage message;
	   
		message.qos = IOT_SAMPLE_MQTT_PUBLISH_QOS;
		message.retained = 0;
		message.payload = (void*)telemetry_message_payloads[message_count];
		message.payloadlen = strlen(message.payload);

		if ((rc = MQTTPublish(&mqtt_client, telemetry_topic_buffer, &message)) != 0)
		{
			IOT_SAMPLE_LOG_ERROR("Failed to publish Telemetry message #%d: MQTTClient return code %d.", message_count + 1, rc);
	    }
	    IOT_SAMPLE_LOG_SUCCESS("Message #%d: Client published the Telemetry message.", message_count + 1);
	    IOT_SAMPLE_LOG("Payload: %s\n", telemetry_message_payloads[message_count]);

	    iot_sample_sleep_for_seconds(TELEMETRY_SEND_INTERVAL_SEC);
	}
}

static void disconnect_mqtt_client_from_iot_hub(void)
{
	int rc = MQTTDisconnect(&mqtt_client);
	if (rc != SUCCESS)
	{
		IOT_SAMPLE_LOG_ERROR("Failed to disconnect MQTT client: MQTTClient return code %d.", rc);
	}
	mqtt_network.disconnect(&mqtt_network);
}

/*
 * This sample sends five telemetry messages to the Azure IoT Hub. X509 self-certification is used.
 */
static void example_azure_iot_hub_telemetry_thread(void* param)
{

	while (wifi_is_ready_to_transceive(RTW_STA_INTERFACE) != SUCCESS) {
		vTaskDelay(1000);
	}

	create_and_configure_mqtt_client();
	IOT_SAMPLE_LOG_SUCCESS("Client created and configured.");

	connect_mqtt_client_to_iot_hub();
	IOT_SAMPLE_LOG_SUCCESS("Client connected to IoT Hub.\n");

	send_telemetry_messages_to_iot_hub();
	IOT_SAMPLE_LOG_SUCCESS("Client sent telemetry messages to IoT Hub.");

	disconnect_mqtt_client_from_iot_hub();
	IOT_SAMPLE_LOG_SUCCESS("Client disconnected from IoT Hub.");

	vTaskDelete(NULL);
}

void example_azure_iot_hub_telemetry(void)
{
	if(xTaskCreate(example_azure_iot_hub_telemetry_thread, ((const char*)"example_aziot_telemetry_thread"), 8000, NULL, tskIDLE_PRIORITY + 1, NULL) != pdPASS)
		printf("\n\r%s xTaskCreate(example_aziot_telemetry_thread) failed", __FUNCTION__);
}

#endif
