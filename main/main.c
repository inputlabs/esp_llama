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

#define UART_PIN_TX 16
#define UART_PIN_RX 17
#define UART_RATE 1 * 1000 * 1000  // 115200
#define UART_BUFFER_SIZE 256
#define UART_MSG_LEN 16
#define UART_TIMEOUT 100  // ms

#define SEND_RATE 4  // ms


static uint8_t MAC_DONGLE[6];
static uint8_t MAC_CONTROLLER[6];
static uint8_t MAC_C6_DEVBOARD_A[6] = {0xF0, 0xF5, 0xBD, 0x05, 0xDB, 0xA8};
static uint8_t MAC_C6_DEVBOARD_B[6] = {0xF0, 0xF5, 0xBD, 0x05, 0xCD, 0x2C};
static uint8_t MAC_C2_DEVBOARD_A[6] = {0x08, 0x3a, 0x8d, 0x40, 0xd8, 0x00};
static uint8_t MAC_C2_BREAKOUT_A[6] = {0x80, 0x64, 0x6f, 0x41, 0x02, 0x60};

static int64_t uint8_to_int64(uint8_t *array) {
    int64_t value = 0;
    for (uint8_t i=0; i<8; i++) {
        value |= ((int64_t)array[i]) << (i * 8);
    }
    return value;
}

static void wifi_init(void) {
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE));
}

static void uart_init() {
    uart_config_t uart_config = {
        .baud_rate  = UART_RATE,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_set_pin(UART_NUM_0, 6, 7, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

    ESP_ERROR_CHECK(uart_driver_install(UART_NUM_1, UART_BUFFER_SIZE, UART_BUFFER_SIZE, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(UART_NUM_1, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(UART_NUM_1, UART_PIN_TX, UART_PIN_RX, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

    ESP_ERROR_CHECK(uart_set_pin(UART_NUM_0, 4, 5, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
}

static void dongle_task() {
    while (true) {
        vTaskDelay(1000);
    }

    // uint8_t *data = (uint8_t *)malloc(UART_MSG_LEN);
    // while(true) {
    //     uart_wait_tx_idle_polling(UART_NUM_1);
    //     uint8_t data[UART_MSG_LEN] = {0,};
    //     int64_t timestamp = esp_timer_get_time();
    //     memcpy(data, &timestamp, 8);
    //     data[15] = 255;
    //     uint8_t len = uart_write_bytes(UART_NUM_1, (char*)data, UART_MSG_LEN);
    //     if (len != UART_MSG_LEN) {
    //         ESP_LOGI("SEND ERROR", "%i", len);
    //     }
    //     // ESP_LOGI("UART_TX", "Sent %i %i %i %i %i %i %i %i ", data[0], data[1], data[2], data[3], data[4], data[5], data[6], data[7]);
    //     // ESP_LOGI("TICK", "%i %i %li %li", configTICK_RATE_HZ, SEND_RATE, portTICK_PERIOD_MS, SEND_RATE / portTICK_PERIOD_MS);
    //     vTaskDelay(SEND_RATE / portTICK_PERIOD_MS);
    // }
    // free(data);
}

static void controller_task() {
    // uint8_t* data = (uint8_t*)malloc(16);
    uint8_t data[32] = {0,};


    while(true) {
        int64_t now = esp_timer_get_time();
        memcpy(data, (uint8_t*)&now, 8);

        uint8_t code = esp_now_send(MAC_DONGLE, data, 16);
        // ESP_LOGI("CONTROLLER_TASK", "esp_now_send() code=%i", code);
        vTaskDelay(4);

        // size_t pending = 0;
        // uart_get_buffered_data_len(UART_NUM_1, &pending);
        // ESP_LOGI("PENDING", "%i", pending);

        // int8_t len = uart_read_bytes(UART_NUM_1, data, UART_MSG_LEN, UART_TIMEOUT);
        // if (len == UART_MSG_LEN) {

        //     if (data[15] == 255) {
        //         // ESP_LOGI(
        //         //     "CONTROLLER",
        //         //     "Combined (%i) %i %i %i %i %i %i %i %i", len,
        //         //     data[0], data[1],  data[2],  data[3],  data[4],  data[5],  data[6],  data[7]
        //         //     // data[8], data[9], data[10], data[11], data[12], data[13], data[14], data[15]
        //         // );

        //         uint8_t err = esp_now_send(MAC_DONGLE, data, UART_MSG_LEN);
        //         if (err) ESP_LOGI("ESPNOW_TX", "error=%i", err);
        //     } else {
        //         ESP_LOGE("CONTROLLER", "Termination bit %i", data[15]);
        //     }
        // } else if (len == 0) {
        //     vTaskDelay(1 / portTICK_PERIOD_MS);
        // } else {
        //     ESP_LOGE("CONTROLLER", "UART error %i", len);
        // }
    }
    // free(data);
}

static void espnow_dongle_callback(
    const esp_now_recv_info_t *recv_info,
    const uint8_t *data,
    int len
) {
    // ESP_LOGI("DONGLE_CB", "data %i %i %i %i ", data[0], data[1], data[2], data[3]);
    esp_now_send(MAC_CONTROLLER, data, len);
}

static void espnow_controller_callback(
    const esp_now_recv_info_t *recv_info,
    const uint8_t *data,
    int len
) {
    static uint16_t iter = 0;
    static float sum = 0;
    static int64_t last_print = 0;
    int64_t ts;
    memcpy(&ts, data, 8);
    int64_t now = esp_timer_get_time();
    int64_t roundtrip = (now - ts);
    sum += roundtrip;
    iter++;
    if (now - last_print > 250*1000) {
        ESP_LOGI("CONTROLLER_CB", "roundtrip_avg=%.0f packets=%i", sum/iter, iter);
        last_print = now;
        iter = 0;
        sum = 0;
    }
}

// static void espnow_callback_1(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len) {
//     uint8_t err = esp_now_send(MAC_DONGLE, data, 8);
// }

// static void espnow_callback_2(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len) {
//     static int64_t last = 0;

//     int64_t now = esp_timer_get_time();
//     int64_t elapsed = now - last;
//     last = now;

//     int64_t ts = uint8_to_int64((uint8_t *)data);
//     int64_t latency = now - ts;

//     static float elapsed_sum = 0;
//     static float latency_sum = 0;
//     static float elapsed_max = 0;
//     static float latency_max = 0;
//     elapsed_sum += elapsed;
//     latency_sum += latency;
//     if (elapsed > elapsed_max) elapsed_max = elapsed;
//     if (latency > latency_max) latency_max = latency;
//     static uint16_t iter = 0;
//     iter++;
//     float samples = 1000 / SEND_RATE;
//     if (iter == samples) {
//         float elapsed_avg = (elapsed_sum / samples) / 1000;
//         float latency_avg = (latency_sum / samples) / 1000;
//         ESP_LOGI(
//             "",
//             "ELAPSED avg=%0.1f max=%0.1f | LATENCY avg=%0.1f max=%0.1f",
//             elapsed_avg, elapsed_max/1000, latency_avg, latency_max/1000
//         );
//         elapsed_sum = 0;
//         latency_sum = 0;
//         elapsed_max = 0;
//         latency_max = 0;
//         iter = 0;
//     }
// }


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
    ESP_LOGI("ADD_PEER", "MAC %02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, mac, 6);
    peer.channel = ESPNOW_CHANNEL;
    peer.encrypt = true;
    esp_err_t ret = esp_now_add_peer(&peer);
    ESP_ERROR_CHECK(ret);
}

void app_main(void) {
    memcpy(MAC_DONGLE, MAC_C2_DEVBOARD_A, 6);
    memcpy(MAC_CONTROLLER, MAC_C2_BREAKOUT_A, 6);

    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK( nvs_flash_erase() );
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // uart_init();
    wifi_init();
    esp_now_init();

    uint8_t mac[6];
    get_mac(mac);
    ESP_LOGI("MAIN", "MAC %02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    // Dongle.
    if (compare_mac(mac, MAC_DONGLE)) {
        ESP_LOGI("MAIN", "INIT dongle");
        add_peer(MAC_CONTROLLER);
        esp_now_register_recv_cb(espnow_dongle_callback);
        xTaskCreate(dongle_task, "dongle", TASK_STACK, NULL, 10, NULL);
    }

    // Controller.
    if (compare_mac(mac, MAC_CONTROLLER)) {
        ESP_LOGI("MAIN", "INIT controller");
        add_peer(MAC_DONGLE);
        esp_now_register_recv_cb(espnow_controller_callback);
        xTaskCreate(controller_task, "controller", TASK_STACK, NULL, 10, NULL);
    }
}
