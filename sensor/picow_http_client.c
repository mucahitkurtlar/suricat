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

#define VOLTAGE_LIMIT 2.0f
#define SLEEP_DURATION_MS 300

#define SENSOR_PIN 26

#if !defined(HTTP_SERVER_IP)
#error HTTP_SERVER_IP not defined
#endif

#define TCP_PORT 8000
#define DEBUG_printf printf
#define BUF_SIZE 2048

#define TEST_ITERATIONS 10
#define POLL_TIME_S 5

#if 0
static void dump_bytes(const uint8_t* bptr, uint32_t len) {
    unsigned int i = 0;

    printf("dump_bytes %d", len);
    for (i = 0; i < len;) {
        if ((i & 0x0f) == 0) {
            printf("\n");
        }
        else if ((i & 0x07) == 0) {
            printf(" ");
        }
        printf("%02x ", bptr[i++]);
    }
    printf("\n");
}
#define DUMP_BYTES dump_bytes
#else
#define DUMP_BYTES(A,B)
#endif

typedef struct TCP_CLIENT_T_ {
    struct tcp_pcb* tcp_pcb;
    ip_addr_t remote_addr;
    uint8_t buffer[BUF_SIZE];
    int buffer_len;
    bool connected;
} TCP_CLIENT_T;

static err_t tcp_client_close(void* arg) {
    TCP_CLIENT_T* state = (TCP_CLIENT_T*)arg;
    err_t err = ERR_OK;
    if (state->tcp_pcb != NULL) {
        tcp_arg(state->tcp_pcb, NULL);
        tcp_poll(state->tcp_pcb, NULL, 0);
        tcp_sent(state->tcp_pcb, NULL);
        tcp_recv(state->tcp_pcb, NULL);
        tcp_err(state->tcp_pcb, NULL);
        err = tcp_close(state->tcp_pcb);
        if (err != ERR_OK) {
            DEBUG_printf("close failed %d, calling abort\n", err);
            tcp_abort(state->tcp_pcb);
            err = ERR_ABRT;
        }
        state->tcp_pcb = NULL;
    }
    return err;
}

// Called with results of operation
static err_t tcp_result(void* arg, int status) {
    TCP_CLIENT_T* state = (TCP_CLIENT_T*)arg;
    if (status == 0) {
        DEBUG_printf("test success\n");
    }
    else {
        DEBUG_printf("test failed %d\n", status);
    }

    return tcp_client_close(arg);
}

static err_t tcp_client_sent(void* arg, struct tcp_pcb* tpcb, u16_t len) {
    TCP_CLIENT_T* state = (TCP_CLIENT_T*)arg;
    DEBUG_printf("tcp_client_sent %u\n", len);

    tcp_result(arg, 0);
    
    return ERR_OK;
}

static err_t tcp_client_connected(void* arg, struct tcp_pcb* tpcb, err_t err) {
    TCP_CLIENT_T* state = (TCP_CLIENT_T*)arg;
    if (err != ERR_OK) {
        printf("connect failed %d\n", err);
        return tcp_result(arg, err);
    }
    state->connected = true;
    DEBUG_printf("Waiting for buffer from server\n");
    return ERR_OK;
}

static err_t tcp_client_poll(void* arg, struct tcp_pcb* tpcb) {
    DEBUG_printf("tcp_client_poll\n");
    return tcp_result(arg, -1); // no response is an error?
}

static void tcp_client_err(void* arg, err_t err) {
    if (err != ERR_ABRT) {
        DEBUG_printf("tcp_client_err %d\n", err);
        tcp_result(arg, err);
    }
}

static bool tcp_client_open(void* arg) {
    TCP_CLIENT_T* state = (TCP_CLIENT_T*)arg;
    DEBUG_printf("Connecting to %s port %u\n", ip4addr_ntoa(&state->remote_addr), TCP_PORT);
    state->tcp_pcb = tcp_new_ip_type(IP_GET_TYPE(&state->remote_addr));
    if (!state->tcp_pcb) {
        DEBUG_printf("failed to create pcb\n");
        return false;
    }

    tcp_arg(state->tcp_pcb, state);
    tcp_poll(state->tcp_pcb, tcp_client_poll, POLL_TIME_S * 2);
    tcp_sent(state->tcp_pcb, tcp_client_sent);
    tcp_recv(state->tcp_pcb, NULL);
    tcp_err(state->tcp_pcb, tcp_client_err);

    state->buffer_len = 0;

    // cyw43_arch_lwip_begin/end should be used around calls into lwIP to ensure correct locking.
    // You can omit them if you are in a callback from lwIP. Note that when using pico_cyw_arch_poll
    // these calls are a no-op and can be omitted, but it is a good practice to use them in
    // case you switch the cyw43_arch type later.
    cyw43_arch_lwip_begin();
    err_t err = tcp_connect(state->tcp_pcb, &state->remote_addr, TCP_PORT, tcp_client_connected);
    char req[1024];
    char* reqtemplate = "POST /api/v1/sensor/sensor1 HTTP/1.1\r\n\r\n";
    int reqlen = snprintf(req, BUF_SIZE, reqtemplate);
    tcp_write(state->tcp_pcb, req, reqlen, TCP_WRITE_FLAG_COPY);
    cyw43_arch_lwip_end();

    return err == ERR_OK;
}

// Perform initialisation
static TCP_CLIENT_T* tcp_client_init(void) {
    TCP_CLIENT_T* state = calloc(1, sizeof(TCP_CLIENT_T));
    if (!state) {
        DEBUG_printf("failed to allocate state\n");
        return NULL;
    }
    ip4addr_aton(HTTP_SERVER_IP, &state->remote_addr);
    return state;
}

void make_request(void) {
    TCP_CLIENT_T* state = tcp_client_init();
    if (!state) {
        return;
    }
    if (!tcp_client_open(state)) {
        tcp_result(state, -1);
        return;
    }
    free(state);
}

int main() {
    bool is_sensor_detected = false;

    stdio_init_all();
    adc_init();

    adc_gpio_init(SENSOR_PIN);
    adc_select_input(0);


    if (cyw43_arch_init()) {
        DEBUG_printf("failed to initialise\n");
        return 1;
    }
    cyw43_arch_enable_sta_mode();

    printf("Connecting to WiFi...\n");
    if (cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASSWORD, CYW43_AUTH_WPA2_AES_PSK, 30000)) {
        printf("failed to connect.\n");
        return 1;
    }
    else {
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
            make_request();
            printf("Request sent to %s\n", HTTP_SERVER_IP);
            is_sensor_detected = true;
        } else if (voltage >= VOLTAGE_LIMIT && is_sensor_detected) {
            printf("Sensor back to normal. Sending request to server...\n");
            make_request();
            printf("Request sent to %s\n", HTTP_SERVER_IP);
            is_sensor_detected = false;
        }
        
        sleep_ms(SLEEP_DURATION_MS);
    }
    cyw43_arch_deinit();
    return 0;
}