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

typedef enum _UART_AT {
    AT_HID = 1,
    AT_WEBUSB,
    AT_BATTERY,
} UART_AT;

static uint8_t MAC_BROADCAST[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
// static uint8_t MAC_C6_DEVBOARD_A[6] = {0xF0, 0xF5, 0xBD, 0x05, 0xDB, 0xA8};
// static uint8_t MAC_C6_DEVBOARD_B[6] = {0xF0, 0xF5, 0xBD, 0x05, 0xCD, 0x2C};
// static uint8_t MAC_C2_DEVBOARD_A[6] = {0x08, 0x3a, 0x8d, 0x40, 0xd8, 0x00};
// static uint8_t MAC_C2_BREAKOUT_A[6] = {0x80, 0x64, 0x6f, 0x41, 0x02, 0x60};
// static uint8_t MAC_DONGLE[6];
// static uint8_t MAC_CONTROLLER[6];


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

static void uart_read_task() {
    static uint8_t i = 0;
    static uint8_t command = 0;
    static uint8_t payload[68] = {0,};
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
            if ((i==0 && c==30) || (i==1 && c==29) || (i==2 && c==28))  {
                i += 1;
            } else {
                i = 0;
            }
        }
        // Get AT command.
        else if (i == 3) {
            if (c >= AT_HID && c <= AT_BATTERY) {
                command = c;
                i += 1;
            } else {
                i = 0;
                printf("ESP: AT command unknown (%i)\n", c);
            }
        }
        // Get payload.
        else {
            payload[i-4] = c;
            i += 1;
            // Payload complete.
            if (command==AT_HID && i==4+32) {
                // ESP send HID.
                uint8_t message[33] = {AT_HID, 0,};
                memcpy(&message[1], payload, 32);
                uint8_t err_send = esp_now_send(MAC_BROADCAST, message, 33);
                if (err_send) printf("ESP: esp_now_send error=%i\n", err_send);
                i = 0;
                continue;
            }
            if (command==AT_WEBUSB && i==4+64) {
                // ESP send WEBSUB.
                uint8_t message[65] = {AT_WEBUSB, 0,};
                memcpy(&message[1], payload, 64);
                uint8_t err_send = esp_now_send(MAC_BROADCAST, message, 65);
                if (err_send) printf("ESP: esp_now_send error=%i\n", err_send);
                i = 0;
                continue;
            }
        }
    }
}

static void espnow_callback(
    const esp_now_recv_info_t *recv_info,
    const uint8_t *data,
    int len
) {
    char message[68] = {30, 29, 28, 0,};
    memcpy(&message[3], data, len);
    uint8_t message_len = 3 + len;
    uint8_t sent = uart_write_bytes(UART_NUM_0, message, message_len);
    if (sent != message_len) printf("ESP: uart_write_bytes error\n");
}

// static void mock_task_1() {
//     while(true) {
//         char control[4] = {16, 32, 64, 128,};
//         char payload[32] = {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31};
//         uint8_t sent = 0;
//         sent = uart_write_bytes(UART_NUM_0, control, 4);
//         if (sent != 4) printf("UART write error\n");
//         sent = uart_write_bytes(UART_NUM_0, payload, 32);
//         if (sent != 32) printf("UART write error\n");
//         vTaskDelay(2000);
//     }
// }

// static void mock_task_2() {
//     while(true) {
//         uint8_t payload[32] = {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31};
//         uint8_t err = esp_now_send(MAC_BROADCAST, payload, 32);
//         printf("send error=%i\n", err);
//         vTaskDelay(2000);
//     }
// }

// static void mock_callback_1(
//     const esp_now_recv_info_t *recv_info,
//     const uint8_t *data,
//     int len
// ) {
//     char control[4] = {16, 32, 64, 128};
//     // char message[32] = {0,};
//     // memcpy(message, payload, 32);
//     // memcpy(message, payload, 32);
//     uint8_t sent;
//     sent = uart_write_bytes(UART_NUM_0, control, 4);
//     if (sent != 4) printf("UART write error\n");
//     sent = uart_write_bytes(UART_NUM_0, data, len);
//     if (sent != len) printf("UART write error\n");
// }

void battery_level_task() {
    // Init ADC.
    adc_oneshot_unit_init_cfg_t unit_config = {
        .unit_id = ADC_UNIT_1,
    };
    adc_oneshot_chan_cfg_t channel_config = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = ADC_ATTEN_DB_12,
    };
    adc_oneshot_unit_handle_t adc_unit;
    adc_oneshot_new_unit(&unit_config, &adc_unit);
    adc_oneshot_config_channel(adc_unit, ADC_CHANNEL_0, &channel_config);
    // Init switchable ground reference GPIO.
    gpio_set_direction(GPIO_NUM_18, GPIO_MODE_INPUT);
    // Start task.
    static uint32_t battery_level = 0;
    while(true) {
        // Pull down GPIO 18.
        gpio_pulldown_en(GPIO_NUM_18);
        vTaskDelay(1);  // Give time to settle.
        // Measure in GPIO 0.
        uint32_t value;
        adc_oneshot_read(adc_unit, ADC_CHANNEL_0, (int*)&value);
        if (battery_level == 0) {
            battery_level = value;
        } else {
            battery_level = ((battery_level * 9) + value) / 10;
        }
        // Float GPIO 18.
        gpio_pulldown_dis(GPIO_NUM_18);
        // Delay.
        // printf("Battery level: %lu\n", battery_level);
        uint8_t message[8] = {30, 29, 28, AT_BATTERY, 0, 0, 0, 0};
        memcpy(&message[4], &battery_level, 4);
        uint8_t sent = uart_write_bytes(UART_NUM_0, message, 8);
        if (sent != 8) printf("ESP: uart_write_bytes error\n");
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
    // memcpy(MAC_CONTROLLER, MAC_C2_DEVBOARD_A, 6);
    // memcpy(MAC_DONGLE, MAC_C2_BREAKOUT_A, 6);

    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK( nvs_flash_erase() );
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    uart_init();
    wlan_init();
    esp_now_init();
    add_peer(MAC_BROADCAST);

    printf("ESP: Init RTOS tasks\n");
    esp_now_register_recv_cb(espnow_callback);
    xTaskCreate(uart_read_task, "uart_read", TASK_STACK, NULL, 10, NULL);
    xTaskCreate(battery_level_task, "battery_level", TASK_STACK, NULL, 11, NULL);

    // xTaskCreate(mock_task_1, "task", TASK_STACK, NULL, 10, NULL);
    // esp_now_register_recv_cb(mock_callback_1);
}
