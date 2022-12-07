/**
 * Copyright (c) 2022 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <string.h>
#include <time.h>

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"

#include "hardware/gpio.h"
#include "hardware/adc.h"

#include "lwip/pbuf.h"
#include "lwip/tcp.h"

#include "http_client.h"

#define VOLTAGE_LIMIT 2.0f
#define SLEEP_DURATION_MS 300

#define SENSOR_PIN 26

#if !defined(HTTP_SERVER_IP)
#error HTTP_SERVER_IP not defined
#endif

#define TCP_PORT 8000

int main() {
    bool is_sensor_detected = false;

    struct HTTP_Request* activated_request = http_client_create_request("POST", "/api/v1/sensor/sensor1", "activated");
    if (activated_request == NULL) {
        printf("Failed to create request for sensor activation\n");
        return 1;
    }
    struct HTTP_Request* deactivated_request = http_client_create_request("POST", "/api/v1/sensor/sensor1", "deactivated");
    if (deactivated_request == NULL) {
        printf("Failed to create request for sensor deactivation\n");
        return 1;
    }

    stdio_init_all();
    adc_init();

    adc_gpio_init(SENSOR_PIN);
    adc_select_input(0);

    if (cyw43_arch_init()) {
        printf("failed to initialise\n");
        return 1;
    }
    cyw43_arch_enable_sta_mode();

    printf("Connecting to WiFi...\n");
    if (cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASSWORD, CYW43_AUTH_WPA2_AES_PSK, 30000)) {
        printf("failed to connect.\n");
        return 1;
    } else {
        printf("Connected.\n");
    }
    while (true) {
        // 12-bit conversion, assume max value == ADC_VREF == 3.3 V
        const float conversion_factor = 3.3f / (1 << 12);

        uint16_t result = adc_read();
        float voltage = result * conversion_factor;

        printf("Raw value: 0x%03x, voltage: %f V\n", result, voltage);

        if (voltage < VOLTAGE_LIMIT && !is_sensor_detected) {
            printf("Sensor detected. Sending request to server...\n");
            printf("Request: %s\n", activated_request->complete_request);
            http_client_request(HTTP_SERVER_IP, TCP_PORT, activated_request);
            printf("Request sent to %s\n", HTTP_SERVER_IP);
            is_sensor_detected = true;
        } else if (voltage >= VOLTAGE_LIMIT && is_sensor_detected) {
            printf("Sensor back to normal. Sending request to server...\n");
            printf("Request: %s\n", deactivated_request->complete_request);
            http_client_request(HTTP_SERVER_IP, TCP_PORT, deactivated_request);
            printf("Request sent to %s\n", HTTP_SERVER_IP);
            is_sensor_detected = false;
        }

        sleep_ms(SLEEP_DURATION_MS);
    }
    cyw43_arch_deinit();
    free(activated_request);
    free(deactivated_request);

    return 0;
}