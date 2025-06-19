// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2024, Input Labs Oy.

#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include <math.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/timers.h>
#include <driver/uart.h>
#include <driver/gpio.h>
#include <esp_adc/adc_oneshot.h>
#include <nvs_flash.h>
#include <esp_random.h>
#include <esp_netif.h>
#include <esp_wifi.h>
#include <esp_log.h>
#include <esp_mac.h>
#include <esp_now.h>
#include <esp_timer.h>

#define ESPNOW_CHANNEL 9
#define ESPNOW_PMK "pmk1234567890123"
#define ESPNOW_LMK "lmk1234567890123"

#define TASK_STACK 2048

#define UART_PRIMARY_PIN_TX 20
#define UART_PRIMARY_PIN_RX 19
#define UART_SECONDARY_PIN_TX 7
#define UART_SECONDARY_PIN_RX 6
#define UART_RATE 115200 * 8
#define UART_BUFFER_SIZE 1024
#define UART_CONTROL_0 30
#define UART_CONTROL_1 29
#define UART_CONTROL_2 28
#define UART_CONTROL_BYTES  UART_CONTROL_0, UART_CONTROL_1, UART_CONTROL_2
#define UART_HEADER_LEN 4
#define UART_PAYLOAD_MAX_LEN 68

#define AT_HID_LEN 32
#define AT_WEBUSB_LEN 64
#define AT_BATTERY_LEN 4
#define AT_USB_PROTOCOL_LEN 1

#define TX_20_DB 80
#define TX_18_DB 72
#define TX_16_DB 66
#define TX_15_DB 60
#define TX_14_DB 56
#define TX_13_DB 52
#define TX_11_DB 44
#define TX_8_DB 34
#define TX_7_DB 28
#define TX_5_DB 20
#define TX_2_DB 8

#define BATTERY_INIT_SAMPLES 10
#define BATTERY_AVERAGE_SAMPLES 10

typedef enum _UART_AT {
    AT_HID = 1,
    AT_WEBUSB,
    AT_BATTERY,
    AT_USB_PROTOCOL,
} UART_AT;

// Static.
static uint8_t MAC_BROADCAST[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
static adc_oneshot_unit_handle_t adc_unit;
static float battery_level = 0;

void print_array(uint8_t *array, uint8_t len, bool hex, bool newline) {
    printf("[");
    for(uint8_t i=0; i<len; i++) {
        if (hex) printf("0x%02x ", array[i]);
        else printf("%i ", array[i]);
    }
    printf("]");
    if (newline) printf("\n");
}

static void wlan_init(void) {
    printf("ESP: wlan_init\n");
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE));
    ESP_ERROR_CHECK(esp_wifi_set_max_tx_power(TX_11_DB));
}

static void espnow_send_hid(uint8_t *payload) {
    uint8_t message[1+AT_HID_LEN] = {AT_HID, 0,};
    memcpy(&message[1], payload, AT_HID_LEN);
    uint8_t err_send = esp_now_send(MAC_BROADCAST, message, 1+AT_HID_LEN);
    if (err_send) printf("ESP: espnow_send_hid error=%i\n", err_send);
}

static void espnow_send_webusb(uint8_t *payload) {
    uint8_t message[1+AT_WEBUSB_LEN] = {AT_WEBUSB, 0,};
    memcpy(&message[1], payload, AT_WEBUSB_LEN);
    uint8_t err_send = esp_now_send(MAC_BROADCAST, message, 1+AT_WEBUSB_LEN);
    if (err_send) printf("ESP: espnow_send_webusb error=%i\n", err_send);
}

static void espnow_send_usb_protocol(uint8_t *payload) {
    uint8_t message[1+AT_USB_PROTOCOL_LEN] = {AT_USB_PROTOCOL, 0,};
    memcpy(&message[1], payload, AT_USB_PROTOCOL_LEN);
    uint8_t err_send = esp_now_send(MAC_BROADCAST, message, 1+AT_USB_PROTOCOL_LEN);
    if (err_send) printf("ESP: espnow_send_usb_protocol error=%i\n", err_send);
}

// Redirect incomming ESPNOW messages to UART.
static void espnow_callback(
    const esp_now_recv_info_t *recv_info,
    const uint8_t *data,
    int len
) {
    char message[UART_PAYLOAD_MAX_LEN] = {UART_CONTROL_0, UART_CONTROL_1, UART_CONTROL_2, 0,};
    memcpy(&message[3], data, len);
    uint8_t message_len = 3 + len;
    uint8_t sent = uart_write_bytes(UART_NUM_0, message, message_len);
    if (sent != message_len) printf("ESP: uart_write_bytes error\n");
}

static void uart_init() {
    printf("ESP: uart_init\n");
    // Reinitialization needed to enable RX.
    uart_config_t uart_config = {
        .baud_rate  = UART_RATE,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_driver_install(UART_NUM_0, UART_BUFFER_SIZE, UART_BUFFER_SIZE, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(UART_NUM_0, &uart_config));
}

// Redirect incomming UART messages to ESPNOW.
static void uart_read_task() {
    static uint8_t i = 0;
    static uint8_t command = 0;
    static uint8_t payload[UART_PAYLOAD_MAX_LEN] = {0,};
    while(true) {
        uint16_t pending = 0;
        uint8_t err_read = uart_get_buffered_data_len(UART_NUM_0, (size_t*)&pending);
        if (err_read) printf("ESP: uart_get_buffered_data_len error=%i\n", err_read);
        if (pending == 0) {
            vTaskDelay(1);
            continue;
        }
        char buffer[1] = {0};
        uart_read_bytes(UART_NUM_0, &buffer, 1, 0);
        char c = buffer[0];
        // Check control bytes.
        if (i < 3) {
            if (
                (i==0 && c==UART_CONTROL_0) ||
                (i==1 && c==UART_CONTROL_1) ||
                (i==2 && c==UART_CONTROL_2)
            ) {
                i += 1;
            } else {
                i = 0;
            }
        }
        // Get AT command.
        else if (i == 3) {
            if (c >= AT_HID && c <= AT_USB_PROTOCOL) {
                command = c;
                i += 1;
            } else {
                i = 0;
                printf("ESP: AT command unknown (%i)\n", c);
            }
        }
        // Get payload.
        else {
            payload[i-UART_HEADER_LEN] = c;
            i += 1;
            // Payload complete.
            if (command==AT_HID && i==UART_HEADER_LEN+AT_HID_LEN) {
                espnow_send_hid(payload);
                i = 0;
                continue;
            }
            if (command==AT_WEBUSB && i==UART_HEADER_LEN+AT_WEBUSB_LEN) {
                espnow_send_webusb(payload);
                i = 0;
                continue;
            }
            if (command==AT_USB_PROTOCOL && i==UART_HEADER_LEN+AT_USB_PROTOCOL_LEN) {
                espnow_send_usb_protocol(payload);
                i = 0;
                continue;
            }
        }
    }
}

float battery_level_read() {
    // Pull down ground reference.
    gpio_pulldown_en(GPIO_NUM_18);
    // Give time to settle.
    vTaskDelay(1);
    // Measure in GPIO 0.
    uint32_t value;
    adc_oneshot_read(adc_unit, ADC_CHANNEL_0, (int*)&value);
    // Stop pulling ground reference (do not drain energy).
    gpio_pulldown_dis(GPIO_NUM_18);
    // Return.
    return (float)value;
}

void battery_level_update() {
    float value = battery_level_read();
    if (battery_level == 0) {
        // Very first read.
        battery_level = value;
    } else {
        // Rolling average with previous samples.
        battery_level = (battery_level * (BATTERY_AVERAGE_SAMPLES-1)) + value;
        battery_level /= BATTERY_AVERAGE_SAMPLES;
    }
}

void battery_level_send_uart() {
    // Prepare message body.
    uint8_t message[UART_HEADER_LEN+AT_BATTERY_LEN] = {UART_CONTROL_BYTES, AT_BATTERY, 0,};
    // Copy payload.
    uint32_t level = (uint32_t)battery_level;  // Float to int.
    memcpy(&message[4], &level, 4);  // 32-bit int size (4x8).
    // Send UART.
    uint8_t sent = uart_write_bytes(UART_NUM_0, message, UART_HEADER_LEN+AT_BATTERY_LEN);
    if (sent != UART_HEADER_LEN+AT_BATTERY_LEN) printf("ESP: battery_level_send_uart error\n");
}

void battery_adc_init() {
    adc_oneshot_unit_init_cfg_t unit_config = {
        .unit_id = ADC_UNIT_1,
    };
    adc_oneshot_chan_cfg_t channel_config = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = ADC_ATTEN_DB_12,
    };
    adc_oneshot_new_unit(&unit_config, &adc_unit);
    adc_oneshot_config_channel(adc_unit, ADC_CHANNEL_0, &channel_config);
    // Init switchable ground reference GPIO.
    gpio_set_direction(GPIO_NUM_18, GPIO_MODE_INPUT);
    // First measurements (to get stable values ASAP).
    for(uint8_t i=0; i<BATTERY_INIT_SAMPLES; i++) {
        battery_level_update();
    }
}

void battery_level_task() {
    while(true) {
        battery_level_update();
        battery_level_send_uart();
        vTaskDelay(10000);
    }
}

void get_mac(uint8_t* mac) {
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
}

bool compare_mac(uint8_t* mac1, uint8_t* mac2) {
    for (uint8_t i=0; i<6; i++) {
        if (mac1[i] != mac2[i]) return false;
    }
    return true;
}

void add_peer(uint8_t* mac) {
    printf("add_peer ");
    print_array(mac, 6, true, true);
    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, mac, 6);
    peer.channel = ESPNOW_CHANNEL;
    if (compare_mac(mac, MAC_BROADCAST)) peer.encrypt = false;
    else peer.encrypt = true;
    esp_err_t ret = esp_now_add_peer(&peer);
    ESP_ERROR_CHECK(ret);
}

void app_main(void) {
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK( nvs_flash_erase() );
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    // Init comms.
    uart_init();
    wlan_init();
    esp_now_init();
    add_peer(MAC_BROADCAST);
    // Init ADC.
    battery_adc_init();
    // Init tasks.
    printf("ESP: Init RTOS tasks\n");
    esp_now_register_recv_cb(espnow_callback);
    xTaskCreate(uart_read_task, "uart_read", TASK_STACK, NULL, 10, NULL);
    xTaskCreate(battery_level_task, "battery_level", TASK_STACK, NULL, 11, NULL);
}
