#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "driver/gpio.h"
#include "esp_log.h"

#include "voltage_detect.h"

#define GPIO_DEBUG_OUTPUT    18 // Debug IO
#define GPIO_DEBUG_OUTPUT_PIN_SEL  (1ULL<<GPIO_DEBUG_OUTPUT)

#define GPIO_5V_DETECTION    4  // 5V power detection
#define GPIO_INPUT_IO_34     34 // Charging status - low when on 5V
#define GPIO_INPUT_IO_35     35 // Vbat voltage divider
#define GPIO_INPUT_PIN_SEL  ((1ULL<<GPIO_INPUT_IO_34) | (1ULL<<GPIO_INPUT_IO_35) | (1ULL<<GPIO_DEBUG_OUTPUT) | (1ULL<<GPIO_5V_DETECTION))

#define ESP_INTR_FLAG_DEFAULT 0

static const char *TAG = "voltage_detect";

static QueueHandle_t g_voltage_detect_event_queue = NULL;
static bool g_on_usb_power = false;

static void IRAM_ATTR gpio_isr_handler(void* arg)
{
    uint32_t gpio_num = (uint32_t) arg;
    xQueueSendFromISR(g_voltage_detect_event_queue, &gpio_num, NULL);
}

// Get GPIO edge change interrupt notifications
static void voltage_detect_task(void* arg)
{
    uint32_t io_num;
    for(;;) 
    {
        if(xQueueReceive(g_voltage_detect_event_queue, &io_num, portMAX_DELAY)) 
        {
            // Do we need to debounce?
            // Check state
            // if different than last confirmed state
            //     if no confirmation timer started, start one for 1ms
            //     if confirmation timer created and running, do nothing
            int level = gpio_get_level(io_num);
            ESP_LOGI(TAG, "5V power: %d", gpio_get_level(io_num));
            gpio_set_level(GPIO_DEBUG_OUTPUT, level); // Mirror 5v power state

            ESP_ERROR_CHECK(esp_event_post(VOLTAGE_EVENTS, level ? VOLTAGE_EVENT_5V_ON : VOLTAGE_EVENT_5V_OFF, NULL, 0, portMAX_DELAY));
        }
    }
}

// Configure 5v power detection pin and edge detection ISR
// Start 5v monitoring task
void voltage_detect_init(void)
{
    int result;
    gpio_config_t io_conf;

    // Configure input pins
    io_conf.intr_type = GPIO_PIN_INTR_DISABLE;
    io_conf.pin_bit_mask = GPIO_INPUT_PIN_SEL;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pull_down_en = 1;
    result = gpio_config(&io_conf);
    if (result != ESP_OK)
    {
        ESP_LOGI(TAG, "gpio config error input\n");
    }

    // Configure output pin
    io_conf.intr_type = GPIO_PIN_INTR_DISABLE;
    io_conf.pin_bit_mask = GPIO_DEBUG_OUTPUT_PIN_SEL;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pull_down_en = 1;
    result = gpio_config(&io_conf);
    if (result != ESP_OK)
    {
        ESP_LOGI(TAG, "gpio config error output\n");
    }

    // Configure 5v edge detection interrupt
    io_conf.intr_type = GPIO_INTR_ANYEDGE;
    io_conf.pin_bit_mask = (1ULL<<GPIO_5V_DETECTION);
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pull_up_en = 1;
    result = gpio_config(&io_conf);
    if (result != ESP_OK)
    {
        ESP_LOGI(TAG, "gpio config error interrupt\n");
    }

    g_on_usb_power = gpio_get_level(GPIO_5V_DETECTION);
    ESP_LOGI(TAG, "USB power: %d", g_on_usb_power);

    g_voltage_detect_event_queue = xQueueCreate(10, sizeof(uint32_t));
    xTaskCreate(voltage_detect_task, "voltage_detect_task", 2048, NULL, 10, NULL);

    gpio_install_isr_service(ESP_INTR_FLAG_DEFAULT);
    gpio_isr_handler_add(GPIO_5V_DETECTION, gpio_isr_handler, (void*) GPIO_5V_DETECTION);
}

void gpio_test(void *pvParameters)
{
    int cnt = 0;
    int statCount = 0;
    while(1)
    {
        vTaskDelay(1000 / portTICK_RATE_MS);
        int statLevel = gpio_get_level(GPIO_INPUT_IO_34);
        if(statLevel)
        {
            statCount++;
        }
        ESP_LOGI(TAG, "GPIO[%d]: %d\n", GPIO_5V_DETECTION, gpio_get_level(GPIO_5V_DETECTION));
        ESP_LOGI(TAG, "GPIO[%d]: %d\n", GPIO_INPUT_IO_34, statLevel);
        ESP_LOGI(TAG, "GPIO[%d]: %d\n", GPIO_INPUT_IO_35, gpio_get_level(GPIO_INPUT_IO_35));
        ESP_LOGI(TAG, "cnt: %d statcount %d\n", cnt++, statCount);
    }
}