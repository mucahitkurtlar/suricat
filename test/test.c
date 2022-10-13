/**
 * Copyright (c) 2022 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "hardware/gpio.h"
#include "hardware/adc.h"

#define VOLTAGE_LIMIT 2.0f
#define SLEEP_DURATION_MS 300
#define CHECK_DURATION_MS 20


int main() {
    bool is_sensor_detected = false;

    stdio_init_all();
    adc_init();

    gpio_init(15);
    gpio_set_dir(15, GPIO_OUT);
    gpio_put(15, 1);
    sleep_ms(1000);
    gpio_put(15, 0);

    adc_gpio_init(26);
    adc_select_input(0);

    while (true) {
        // 12-bit conversion, assume max value == ADC_VREF == 3.3 V
        const float conversion_factor = 3.3f / (1 << 12);

        uint16_t result = adc_read();
        float voltage = result * conversion_factor;
        
        printf("Raw value: 0x%03x, voltage: %f V\n", result, voltage);
        
        if (voltage < VOLTAGE_LIMIT && !is_sensor_detected) {
            printf("Sensor detected. Sending request to server...\n");
            is_sensor_detected = true;
        } else if (voltage >= VOLTAGE_LIMIT && is_sensor_detected) {
            printf("Sensor back to normal. Sending request to server...\n");
            is_sensor_detected = false;
        }
        
        sleep_ms(SLEEP_DURATION_MS);
    }
}