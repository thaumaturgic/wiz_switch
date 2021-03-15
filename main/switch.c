/* BSD Socket API Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.

   .$HOME/esp/esp-idf/export.ps1

*/
#include <string.h>
#include <sys/param.h>

#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "protocol_examples_common.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>
#include "addr_from_stdin.h"

#include "voltage_detect.h"

#define HOST_IP_ADDR "255.255.255.255"
#define BROADCAST_IP "255.255.255.255"
#define PORT 38899

#define SWITCH_BULB_DISCOVER_TIMEOUT_MS 5000
#define SWITCH_BULB_DISCOVER_POLL_MS 100

#define MAX_BULBS 10

typedef struct wiz_bulb
{
    char mac[14];
    in_addr_t ip;
    bool on;
}wiz_bulb_t;

static wiz_bulb_t g_bulbs[MAX_BULBS];
static int g_bulbs_discovered = 0;

static const char *TAG = "switch";

static const char *JSON_DELIMITERS = "{}\":, ";
static const char *registration_payload = "{\"method\":\"registration\",\"params\":{\"phoneMac\":\"AAAAAAAAAAAA\",\"register\":false,\"phoneIp\":\"1.2.3.4\",\"id\":\"1\"}}";
//static const char *payload_off = "{\"method\":\"setPilot\",\"params\":{\"state\":false}}";
//static const char *payload_on = "{\"method\":\"setPilot\",\"params\":{\"state\":true}}";

static bool switch_add_bulb(in_addr_t ip_address, char * mac_address)
{

    ESP_LOGI(__func__, "ip %d: mac %s", ip_address, mac_address);

    for(int i = 0; i < MAX_BULBS; i++)
    {
        wiz_bulb_t *bulb = &g_bulbs[i];
        ESP_LOGV(__func__, "bulb %d ip %d mac %s", i, bulb->ip, bulb->mac);

        if(!bulb->mac[0])
        {
            ESP_LOGV(__func__, "added bulb");
            strcpy(bulb->mac, mac_address);
            bulb->ip = ip_address;
            g_bulbs_discovered++;
            return true;
        }
        else if(strncmp(bulb->mac, mac_address, strlen(bulb->mac)) == 0)
        {
            bulb->ip = ip_address;
            return false;
        }
    }

    return false;
}

static void switch_print_bulbs(wiz_bulb_t bulbs[], int length)
{
    for(int i = 0; i < length; i++)
    {
        ESP_LOGI(TAG, "bulb %d ip %s mac %s", i, inet_ntoa(bulbs[i].ip), bulbs[i].mac);
    }
}

// Broadcast to the network on the wiz UDP port
// Listen for responses, parse the MAC and IP and add the bulb to the list 
// AFter 5 seconds of no responses, close the socket and return
static int switch_discover_bulbs(bool clear_list)
{
    const char *TAG = __func__;

    if(clear_list)
    {
        memset(g_bulbs, 0x00, sizeof(g_bulbs));
        g_bulbs_discovered = 0;        
    }
    
    int bulbs_found = 0;
    bool bulb_found_this_pass = false;
    int timeout_count = 0;

    // Open socket to broadcast on
    char rx_buffer[256];
    memset(rx_buffer, 0x00, sizeof(rx_buffer));

    struct sockaddr_in broadcast_address;
    broadcast_address.sin_addr.s_addr = inet_addr(BROADCAST_IP);
    broadcast_address.sin_family = AF_INET;
    broadcast_address.sin_port = htons(PORT);

    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0) 
    {
        ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
        return -1;
    }
    ESP_LOGI(TAG, "Socket created, broadcasting to %s:%d", BROADCAST_IP, PORT);

    int err = sendto(sock, registration_payload, strlen(registration_payload), 0, (struct sockaddr *)&broadcast_address, sizeof(broadcast_address));
    if (err < 0) 
    {
        ESP_LOGE(TAG, "Error occurred during broadcast: errno %d", errno);
        return -1;
    }

    struct sockaddr_in response_addr;
    socklen_t socklen = sizeof(response_addr);

    while(1)
    {
        int len = recvfrom(sock, rx_buffer, sizeof(rx_buffer) - 1, MSG_DONTWAIT, (struct sockaddr *)&response_addr, &socklen);

        if (len > 0) 
        {
            rx_buffer[len] = 0;
            ESP_LOGI(TAG, "Received %d bytes from %s:", len, inet_ntoa(response_addr.sin_addr));
            ESP_LOGV(TAG, "%s", rx_buffer);

            char * token = strtok(rx_buffer, JSON_DELIMITERS);
            while(strncmp(token, "mac", 3) != 0)
            {
                token = strtok(NULL, JSON_DELIMITERS);
            }

            if(switch_add_bulb(response_addr.sin_addr.s_addr, strtok(NULL, JSON_DELIMITERS)))
            {
                bulbs_found++;
            }
            
            bulb_found_this_pass = true;
        }
        else 
        {
            ESP_LOGV(TAG, "recvfrom failed: errno %d", errno);
        }

        vTaskDelay(SWITCH_BULB_DISCOVER_POLL_MS / portTICK_PERIOD_MS);

        if(bulb_found_this_pass)
        {
            ESP_LOGV(TAG, "found bulb, resetting timeout %d ", timeout_count);
            timeout_count = 0;
        }

        if(timeout_count++ == (SWITCH_BULB_DISCOVER_TIMEOUT_MS / SWITCH_BULB_DISCOVER_POLL_MS))
        {
            ESP_LOGI(TAG, "timeout: no bulbs responded for %d seconds", SWITCH_BULB_DISCOVER_TIMEOUT_MS / 1000);
            break;
        }

        bulb_found_this_pass = false;
    }
    
    if(sock != -1)
    {
        shutdown(sock, 0);
        close(sock);    
    }

    return bulbs_found;
}


static void udp_client_task(void *pvParameters)
{
    // Run state machine with states
    // State machine poll logic:
    //  if a GPIO is set high/low 
    //      go to setup state -> Start soft AP, setup http server, user inputs wifi credentials for real network, scan other network, collect bulbs
    //  else 
    //      if on main power
    //          go to ON state -> turn on stored lights
    //      if off main power 
    //          go to OFF state -> turn off stored lights
    //          go to sleep state
    while (1) 
    {
        switch_discover_bulbs(true);
        ESP_LOGI(TAG, "Found %d bulbs", g_bulbs_discovered);
        switch_print_bulbs(g_bulbs, g_bulbs_discovered);

        int new_bulbs = switch_discover_bulbs(false);
        ESP_LOGI(TAG, "Found %d more bulbs", new_bulbs);
        switch_print_bulbs(g_bulbs, g_bulbs_discovered);

        vTaskDelay(60000 / portTICK_PERIOD_MS);

        // //------
        // char rx_buffer[256];
        // struct sockaddr_in dest_addr_nightstand;
        // dest_addr_nightstand.sin_addr.s_addr = inet_addr("192.168.1.8");
        // dest_addr_nightstand.sin_family = AF_INET;
        // dest_addr_nightstand.sin_port = htons(PORT);
        // int sock_nightstand = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
        // sendto(sock_nightstand, payload_off, strlen(payload_off), 0, (struct sockaddr *)&dest_addr_nightstand, sizeof(dest_addr_nightstand));

        // socklen_t socklen = sizeof(dest_addr_nightstand);
        // int len = recvfrom(sock_nightstand, rx_buffer, sizeof(rx_buffer) - 1, 0, (struct sockaddr *)&dest_addr_nightstand, &socklen);
        // //ESP_LOGI(TAG, "Received %d bytes from %s:", len, host_ip);
        // ESP_LOGI(TAG, "%s", rx_buffer);
        // //------
    }

    vTaskDelete(NULL);
}

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* This helper function configures Wi-Fi or Ethernet, as selected in menuconfig.
     * Read "Establishing Wi-Fi or Ethernet Connection" section in
     * examples/protocols/README.md for more information about this function.
     */
    ESP_ERROR_CHECK(example_connect());

    //xTaskCreate(udp_client_task, "udp_client", 4096, NULL, 5, NULL);
    xTaskCreate(gpio_test, "gpio_test", 4096, NULL, 5, NULL);
}
