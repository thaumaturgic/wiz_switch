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

#define GPIO_OUTPUT_IO_18    18 // Debug IO
#define GPIO_OUTPUT_PIN_SEL  (1ULL<<GPIO_OUTPUT_IO_18)

#define GPIO_INPUT_IO_4      4  // 5V power detection
#define GPIO_INPUT_IO_34     34 // Charging status - low when on 5V
#define GPIO_INPUT_IO_35     35 // Vbat voltage divider
#define GPIO_INPUT_PIN_SEL  ((1ULL<<GPIO_INPUT_IO_34) | (1ULL<<GPIO_INPUT_IO_35) | (1ULL<<GPIO_OUTPUT_IO_18) | (1ULL<<GPIO_INPUT_IO_4))

#define ESP_INTR_FLAG_DEFAULT 0

static QueueHandle_t gpio_evt_queue = NULL;

static void IRAM_ATTR gpio_isr_handler(void* arg)
{
    uint32_t gpio_num = (uint32_t) arg;
    xQueueSendFromISR(gpio_evt_queue, &gpio_num, NULL);
}

static void gpio_task_example(void* arg)
{
    uint32_t io_num;
    for(;;) {
        if(xQueueReceive(gpio_evt_queue, &io_num, portMAX_DELAY)) {
            int level = gpio_get_level(io_num);
            printf("GPIO[%d] intr, val: %d\n", io_num, gpio_get_level(io_num));
            gpio_set_level(GPIO_OUTPUT_IO_18, level);
        }
    }
}

void gpio_test(void *pvParameters)
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
        printf("gpio config error input\n");
    }

    // Configure output pin
    io_conf.intr_type = GPIO_PIN_INTR_DISABLE;
    io_conf.pin_bit_mask = GPIO_OUTPUT_PIN_SEL;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pull_down_en = 1;
    result = gpio_config(&io_conf);
    if (result != ESP_OK)
    {
        printf("gpio config error output\n");
    }

    // Configure 5v edge detection interrupt
    io_conf.intr_type = GPIO_INTR_ANYEDGE;
    io_conf.pin_bit_mask = (1ULL<<GPIO_INPUT_IO_4);
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pull_up_en = 1;
    result = gpio_config(&io_conf);
    if (result != ESP_OK)
    {
        printf("gpio config error interrupt\n");
    }

    //create a queue to handle gpio event from isr
    gpio_evt_queue = xQueueCreate(10, sizeof(uint32_t));
    //start gpio task
    xTaskCreate(gpio_task_example, "gpio_task_example", 2048, NULL, 10, NULL);

    //install gpio isr service
    gpio_install_isr_service(ESP_INTR_FLAG_DEFAULT);
    //hook isr handler for specific gpio pin
    gpio_isr_handler_add(GPIO_INPUT_IO_4, gpio_isr_handler, (void*) GPIO_INPUT_IO_4);

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
        printf("GPIO[%d]: %d\n", GPIO_INPUT_IO_4, gpio_get_level(GPIO_INPUT_IO_4));
        printf("GPIO[%d]: %d\n", GPIO_INPUT_IO_34, statLevel);
        printf("GPIO[%d]: %d\n", GPIO_INPUT_IO_35, gpio_get_level(GPIO_INPUT_IO_35));
        printf("cnt: %d statcount %d\n", cnt++, statCount);
    }
}