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

ESP_EVENT_DEFINE_BASE(VOLTAGE_EVENTS);

static char * g_paired_bulbs[] = {"a8bb5006c224", "a8bb5092f6e1"}; // TODO: Populate this list via UI
static int g_paired_bulbs_count = 2;

typedef struct wiz_bulb
{
    char mac[14];
    in_addr_t ip;
}wiz_bulb_t;

static const char *TAG = "switch";

static wiz_bulb_t g_bulbs[MAX_BULBS];
static int g_bulbs_discovered = 0;

static const char *JSON_DELIMITERS = "{}\":, ";
static const char *REGISTRATION_PAYLOAD = "{\"method\":\"registration\",\"params\":{\"phoneMac\":\"AAAAAAAAAAAA\",\"register\":false,\"phoneIp\":\"1.2.3.4\",\"id\":\"1\"}}";
static const char *PAYLOAD_OFF = "{\"method\":\"setPilot\",\"params\":{\"state\":false}}";
static const char *PAYLOAD_ON = "{\"method\":\"setPilot\",\"params\":{\"state\":true}}";
static const char *SUCCESS_RESPONSE = "\"success\":true";

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

static int switch_socket_create(struct sockaddr_in * socket_struct, in_addr_t ip_address, int port)
{
    socket_struct->sin_addr.s_addr = ip_address;
    socket_struct->sin_family = AF_INET;
    socket_struct->sin_port = htons(port);

    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0) 
    {
        ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
        return -1;
    }
    return sock;
}

static bool switch_send_payload_to_bulb(char * mac_address, const char * payload, int payload_length)
{
    in_addr_t ip_address = 0; 
    struct sockaddr_in socket_send_struct;
    int socket_number; 

    struct sockaddr_in socket_receive_struct;
    socklen_t receive_size = sizeof(socket_receive_struct);
    char rx_buffer[256];

    // Lookup IP for bulb using mac, fail if not found
    for(int i = 0; i < g_bulbs_discovered; i++)
    {
        wiz_bulb_t bulb = g_bulbs[i];
        if(strncmp(bulb.mac, mac_address, strlen(mac_address)) == 0)
        {
            ip_address = bulb.ip;
            break;
        }
    }

    if(ip_address == 0)
    {
        ESP_LOGW(TAG, "No bulb with mac %s found", mac_address);
        return false;
    }

    // Open 'socket' with bulb via IP
    socket_number = switch_socket_create(&socket_send_struct, ip_address, PORT);

    if(socket_number < 0)
        return false;

    // Send payload
    if(sendto(socket_number, payload, payload_length, 0,  (struct sockaddr *)&socket_send_struct, sizeof(socket_send_struct)) < 0)  
        return false;

    // Read a resonse
    memset(rx_buffer, 0x00, sizeof(rx_buffer));
    
    int len = recvfrom(socket_number, rx_buffer, sizeof(rx_buffer) - 1, 0, (struct sockaddr *)&socket_receive_struct, &receive_size);
    rx_buffer[len] = 0;

    shutdown(socket_number, 0);
    close(socket_number);    

    // Parse response for success = true
    return (strstr(rx_buffer, SUCCESS_RESPONSE) != NULL);
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
    struct sockaddr_in broadcast_address;
    int socket_number = switch_socket_create(&broadcast_address, inet_addr(BROADCAST_IP), PORT);

    if (socket_number < 0)
        return -1;

    ESP_LOGI(TAG, "Socket created, broadcasting to %s:%d", BROADCAST_IP, PORT);

    int err = sendto(socket_number, REGISTRATION_PAYLOAD, strlen(REGISTRATION_PAYLOAD), 0, (struct sockaddr *)&broadcast_address, sizeof(broadcast_address));
    if (err < 0) 
    {
        ESP_LOGE(TAG, "Error occurred during broadcast: errno %d", errno);
        return -1;
    }

    // Receive broadcast responses
    char rx_buffer[256];
    memset(rx_buffer, 0x00, sizeof(rx_buffer));
    struct sockaddr_in response_addr;
    socklen_t socklen = sizeof(response_addr);

    while(1)
    {   
        int len = recvfrom(socket_number, rx_buffer, sizeof(rx_buffer) - 1, MSG_DONTWAIT, (struct sockaddr *)&response_addr, &socklen);

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
    
    shutdown(socket_number, 0);
    close(socket_number);

    return bulbs_found;
}

static bool switch_turn_on_paired_bulbs(bool on)
{
    // go through list of paired bulbs
    for(int i = 0; i < g_paired_bulbs_count; i++)
    {
        const char * payload = on ? PAYLOAD_ON : PAYLOAD_OFF;
        // TODO: retry if failed
        bool ok = switch_send_payload_to_bulb(g_paired_bulbs[i], payload, strlen(payload));    
        ESP_LOGI(__func__, "bulb %s success: %d", g_paired_bulbs[i], ok);
    }

    return true;
}

static void switch_5v_on_handler(void* handler_args, esp_event_base_t base, int32_t id, void* event_data)
{
    ESP_LOGW(TAG, "5v on");
    switch_turn_on_paired_bulbs(true);
}

static void switch_5v_off_handler(void* handler_args, esp_event_base_t base, int32_t id, void* event_data)
{
    ESP_LOGW(TAG, "5v off");
    switch_turn_on_paired_bulbs(false);
}

static void switch_init()
{
    switch_discover_bulbs(true);
    ESP_LOGI(TAG, "Found %d bulbs", g_bulbs_discovered);
    switch_print_bulbs(g_bulbs, g_bulbs_discovered);

    int new_bulbs = switch_discover_bulbs(false);
    ESP_LOGI(TAG, "Found %d more bulbs", new_bulbs);
    switch_print_bulbs(g_bulbs, g_bulbs_discovered);
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

    voltage_detect_init();
    switch_init();
    //xTaskCreate(udp_client_task, "udp_client", 4096, NULL, 5, NULL);
    //xTaskCreate(gpio_test, "gpio_test", 4096, NULL, 5, NULL);

    ESP_ERROR_CHECK(esp_event_handler_instance_register(VOLTAGE_EVENTS, VOLTAGE_EVENT_5V_ON, switch_5v_on_handler, NULL, NULL));;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(VOLTAGE_EVENTS, VOLTAGE_EVENT_5V_OFF, switch_5v_off_handler, NULL, NULL));;
}
