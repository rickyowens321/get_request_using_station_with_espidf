/* WiFi station Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <stdio.h>
#include <stdbool.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include <string.h>
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"

#include "lwip/err.h"
#include "lwip/sys.h"

#include "esp_http_client.h"
#include "driver/gpio.h"
#include "cJSON.h"

#define EXAMPLE_ESP_WIFI_SSID      CONFIG_ESP_WIFI_SSID
#define EXAMPLE_ESP_WIFI_PASS      CONFIG_ESP_WIFI_PASSWORD
#define EXAMPLE_ESP_MAXIMUM_RETRY  CONFIG_ESP_MAXIMUM_RETRY

#define MAX_HTTP_OUTPUT_BUFFER 1024

#if CONFIG_ESP_WIFI_AUTH_OPEN
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_OPEN
#elif CONFIG_ESP_WIFI_AUTH_WEP
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WEP
#elif CONFIG_ESP_WIFI_AUTH_WPA_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA2_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA_WPA2_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA_WPA2_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA3_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA3_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA2_WPA3_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_WPA3_PSK
#elif CONFIG_ESP_WIFI_AUTH_WAPI_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WAPI_PSK
#endif

/* This Event group handle is basicly a type of event handler which can be use in freeRTOS to create and control differnt group.*/
static EventGroupHandle_t s_wifi_event_group;
TaskHandle_t blink_task; // this is used to delete the task.

/* The event group allows multiple bits for each event, but we only care about two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1

static const char *TAG = "wifi station";
static const char *TAG1 = "MY_APP_1";
static const char *TAG2 = "HTTP_TAG";
static const char *TAG3 = "JSON_packet";
static int s_retry_num = 0;
static bool led_status = false;
static bool per_status;
static bool is_connected = false;

// This is a event handler which is 
static void event_handler(void *agr, esp_event_base_t event_base,
                         int32_t event_id, void *event_data)
{ 
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        esp_wifi_connect();
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        if (s_retry_num < EXAMPLE_ESP_MAXIMUM_RETRY)
        {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG,"Retry to connect to AP.");
            is_connected = false;
        }
        else{
            xEventGroupSetBits(s_wifi_event_group,WIFI_FAIL_BIT);
        }
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG,"Got IP: "IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(s_wifi_event_group,WIFI_CONNECTED_BIT);
        is_connected = true;
    }
}


void wifi_init_sta(void)
{
    s_wifi_event_group = xEventGroupCreate(); // creates the RTOS group event/task for us..

    ESP_ERROR_CHECK(esp_netif_init()); // check if there is any error in init od network stack.....

    ESP_ERROR_CHECK(esp_event_loop_create_default()); // create the default task/group loop for us...
    esp_netif_create_default_wifi_sta(); //  creates a default network interface in STA mode. It sets up the necessary network configuration as a client....

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT(); //  initializes a Wi-Fi configuration structure cfg with default values
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    
    esp_event_handler_instance_t any_event_id; // These are used to store the instance of event handler and use to unregister them if need...
    esp_event_handler_instance_t got_any_ip; 
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        NULL,
                                                        &any_event_id));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        NULL,
                                                        &got_any_ip));
    
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = EXAMPLE_ESP_WIFI_SSID,
            .password = EXAMPLE_ESP_WIFI_PASS,
            /* Setting a password implies station will connect to all security modes including WEP/WPA.
             * However these modes are deprecated and not advisable to be used. Incase your Access point
             * doesn't support WPA2, these mode can be enabled by commenting below line */
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG,"WIFI has started");

    /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
     * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           WIFI_CONNECTED_BIT|WIFI_FAIL_BIT,
                                           pdFALSE,
                                           pdFALSE,
                                           portMAX_DELAY);

    /* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
     * happened. */
    if(bits & WIFI_CONNECTED_BIT)
    {
        ESP_LOGI(TAG, "connected to ap SSID:%s password:%s",
                 EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
    }
    else if(bits & WIFI_FAIL_BIT)
    {
        ESP_LOGI(TAG,"The Wifi Failed to connect..");
    }
    else{
        ESP_LOGI(TAG,"Some Other Error might have occured..");
    }

    /* The event will not be processed after unregister */
    ESP_ERROR_CHECK(esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, any_event_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, got_any_ip));
    vEventGroupDelete(s_wifi_event_group);
}

void blink_led(void *param)
{
    while(1)
    {
        if(led_status != per_status)
        {
            gpio_set_level(GPIO_NUM_2,led_status);
            per_status = led_status;
        }
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        vTaskDelete(blink_task); 
    }
}


void GET_Method(void *param)
{
    while(1)
    {
        if(is_connected)
        {
            char output_buffer[MAX_HTTP_OUTPUT_BUFFER] = {0};
            esp_http_client_config_t config = {
                .url = "http://parseapi.back4app.com/classes/Hexe", // change the url according to your need.
                //.cert_pem = back4app_root_cert_pem_start
                // Note: i did not add a event handler of this.....
            };
            esp_http_client_handle_t client = esp_http_client_init(&config);

            //GET Request for Server....
            esp_http_client_set_method(client,HTTP_METHOD_GET);
            esp_http_client_set_header(client, "X-Parse-Application-Id", "Place the KEY of ID");
            esp_http_client_set_header(client, "X-Parse-REST-API-Key", "Place the key of API-KEY");

            esp_err_t err = esp_http_client_open(client,0);
            if(err != ESP_OK)
            {
                ESP_LOGI(TAG,"Failed to open HTTP connection: %s", esp_err_to_name(err));
            }
            else
            {
                ESP_LOGI(TAG1,"fetching headers...");
                int HeadersData = esp_http_client_fetch_headers(client);
                if (HeadersData < 0)
                {
                    ESP_LOGE(TAG2, "HTTP client fetch headers failed");
                }
                else
                {
                    ESP_LOGI(TAG1,"Got the headers..");
                    int data_read = esp_http_client_read_response(client,output_buffer,MAX_HTTP_OUTPUT_BUFFER);
                    if (data_read >= 0)
                    {
                        printf("HTTP GET Status = %d\n",
                        esp_http_client_get_status_code(client));
                        ESP_LOGI(TAG2, "DATA RECEV : %s", output_buffer);

                        // Decoding the JSON packet....
                        cJSON *packet = cJSON_Parse(output_buffer);
                        const char *error_ptr = cJSON_GetErrorPtr();
                        if(error_ptr)
                        {
                             ESP_LOGE(TAG3,"converting to json error");
                            ESP_LOGE(TAG3,"ERROR Before [%s]", cJSON_GetErrorPtr());
                        }
                        ESP_LOGI(TAG3,"we are decoding");
                        cJSON* results = cJSON_GetObjectItem(packet, "results");
                        cJSON *first_result = NULL;
                        if (cJSON_IsArray(results))
                        {
                            first_result = cJSON_GetArrayItem(results,0);
                        }
                        if (first_result && cJSON_GetObjectItem(first_result, "led"))
                        {
                            bool status_led = cJSON_IsTrue(cJSON_GetObjectItem(first_result, "led"));
                            ESP_LOGI(TAG3, "led=%d", status_led);
                            led_status = status_led;
                            xTaskCreate(blink_led,"led states", 1024*4, NULL, 2, &blink_task);
                        }
                        else 
                        {
                            ESP_LOGE(TAG3,"There is a error in the led status");
                        }
                        
                        cJSON_Delete(packet);
                    }
                    else
                    {
                        ESP_LOGE(TAG2, "Failed to read response");
                    }
                }
            }
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
        }
        else
        {
            ESP_LOGE(TAG, "WIFI IS NOT AVAILABLE");
        }
        ESP_LOGI(TAG1,"Checking if anythings occurs....");
        vTaskDelay(1000 / portTICK_PERIOD_MS);    
    }   
}


void app_main(void)
{
    //to set the direction of GIPO 2
    gpio_set_direction(GPIO_NUM_2,GPIO_MODE_OUTPUT);

    // Initialize NVS
    esp_err_t ret = nvs_flash_init(); // This is use to init the Non-Volatile Storage in esp32. The return type is "esp_err_t" (which is commoly used to get the error code)
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) //check for error like not free space or if a new version is found.
    {
        ESP_ERROR_CHECK(nvs_flash_erase()); // then erase the NVS space and check for errors.
        ret = nvs_flash_init(); // retrying to init it again.
    }
    ESP_ERROR_CHECK(ret); // checking for other errors if nay other error occurs....

    ESP_LOGI(TAG, "ESP_WIFI_MODE_STA");
    wifi_init_sta(); // to start the station mode of wifi

    xTaskCreate(GET_Method, "gets led value", 1024*4, NULL,1,NULL);
    
}
