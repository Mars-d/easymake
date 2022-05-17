#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "esp_wifi.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"

#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"
#include "lwip/err.h"
#include "lwip/sys.h"

#include "esp_log.h"
#include "mqtt_client.h"
#include "easymake.h"
#include "cJSON.h"
#include "driver/gpio.h"

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1
#define TIMER_PERIOD       1000000

enum {                                       
    AUTO_UPLOAD,
};

static const char *TAG = "WIFI_MQTT";
static int s_retry_num = 5;
static EventGroupHandle_t s_wifi_event_group;

Sensor Sensor_instance[Sensor_MAX];
esp_timer_handle_t TIMER;
esp_mqtt_event_handle_t client;

ESP_EVENT_DEFINE_BASE(DATA_EVENTS);

//WiFi事件回调函数
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < ESP_MAXIMUM_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "retry to connect to the AP");
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI(TAG,"connect to the AP fail");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

//WiFi初始化函数
void wifi_init_sta(void)
{
    //创建事件组
    s_wifi_event_group = xEventGroupCreate();

    //初始化底层 TCP/IP 堆栈
    ESP_ERROR_CHECK(esp_netif_init());

    //Station 无线默认初始化
    esp_netif_create_default_wifi_sta();

    //使用默认的wifi配置->初始化WiFi
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    //新建事件ID->注册事件回调函数  
    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_got_ip));
    //配置WIFI连接参数
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = ESP_WIFI_SSID,
            .password = ESP_WIFI_PASS,
	     .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };

    //开始连接WiFi
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
    ESP_ERROR_CHECK(esp_wifi_start() );
    ESP_LOGI(TAG, "wifi_init_sta finished.");

    //等待WiFi连接成功或者失败（等待事件组）
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);
    
    //判断连接状态
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "connected to ap SSID:%s password:%s",
                 ESP_WIFI_SSID, ESP_WIFI_PASS);
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI(TAG, "Failed to connect to SSID:%s, password:%s",
                 ESP_WIFI_SSID, ESP_WIFI_PASS);
    } else {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
    }

    //释放资源
    ESP_ERROR_CHECK(esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, instance_got_ip));
    ESP_ERROR_CHECK(esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, instance_any_id));
    vEventGroupDelete(s_wifi_event_group);
}

//定时器回调函数
static void timer_callback(void* arg)
{
    esp_event_post(DATA_EVENTS, AUTO_UPLOAD, NULL, 0, portMAX_DELAY);
}

//定时器任务回调函数
static void DataHandle(void* handler_args, esp_event_base_t base, int32_t id, void* event_data)
{
    static int count = 0;
    count++;
    if (count >= AUTO_UPLOAD_TIME) {
        count = 0;
        char udata[100];
        int dlen = SensorQuery(udata,2,Sensor_instance);
        esp_mqtt_client_publish(client, PUB, (const char *)udata, dlen, 0, 0);
        ESP_LOGI(TAG, "Autosent publish successful!");
    }
}

//MQTT回调函数
static void mqtt_event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    //事件捕获日志输出
    ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%d", event_base, event_id);

    //解析事件数据
    esp_mqtt_event_handle_t event = event_data;
    int msg_id;
    switch ((esp_mqtt_event_id_t)event_id) {
    //MQTT连接成功事件
    case MQTT_EVENT_CONNECTED:
        client = event->client;
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
        msg_id = esp_mqtt_client_subscribe(client, SUB , 0);
        ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);
    //MQTT断开连接事件
    case MQTT_EVENT_DISCONNECTED:
        //ESP_ERROR_CHECK(esp_timer_stop(TIMER));
        ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
        break;
    //MQTT订阅成功事件
    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
        ESP_ERROR_CHECK(esp_timer_start_periodic(TIMER, TIMER_PERIOD));
        break;
    //MQTT取消订阅成功事件
    case MQTT_EVENT_UNSUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    //MQTT发布消息成功事件
    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
        break;
    //接收MQTT订阅消息事件
    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "MQTT_EVENT_DATA");
        cJSON *cmd_json = cJSON_Parse(event->data);
        char ppdata[100];
        int dlen = 0;
        if(cmd_json != NULL){
            const cJSON *id  = cJSON_GetObjectItem(cmd_json, "id");
            const cJSON *cmd = cJSON_GetObjectItem(cmd_json, "cmd");
            if(strcmp(id->valuestring,SID) == 0){
                if(cmd->valueint == 0){
                    dlen = SensorQuery(ppdata,0,Sensor_instance);
                }else if(cmd->valueint == 1){
                    int arraylen = cJSON_GetArraySize(cmd_json);
                    for (int i = 2;i<arraylen;i++){ 
                        const cJSON *sen = cJSON_GetArrayItem(cmd_json,i);
                        SensorControl(sen->string,sen->valueint,Sensor_instance);
                    }
                    dlen = SensorQuery(ppdata,1,Sensor_instance);
                }
                esp_mqtt_client_publish(client, PUB, (const char *)ppdata, dlen, 0, 0);
            }
        }
        cJSON_Delete(cmd_json);
        printf("DATA=%.*s\r\n", event->data_len, event->data);
        break;
    default:
        break;
    }
}

void MQTT_loop(void){
    //MQTT服务器相关配置
    esp_mqtt_client_config_t mqtt_cfg = {
        .uri = CONFIG_BROKER_URL,
        .port = CONFIG_PORT,
    };
    //初始化MQTT客户端设置
    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
    //注册事件回调函数
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    //MQTT客户端启动
    esp_mqtt_client_start(client);
}


//程序入口
void app_main(void)
{
    //配置日志输出
    ESP_LOGI(TAG, "[APP] Startup..");
    ESP_LOGI(TAG, "[APP] Free memory: %d bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "[APP] IDF version: %s", esp_get_idf_version());

    //初始化Flash
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    //创建系统默认事件循环
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    //传感器初始化
    sensor_init(Sensor_instance);
    //初始化WIFI
    ESP_LOGI(TAG, "ESP_WIFI_MODE_STA");
    wifi_init_sta();

    //绑定数据事件
    ESP_ERROR_CHECK(esp_event_handler_instance_register(DATA_EVENTS,AUTO_UPLOAD , DataHandle, NULL, NULL));
    //配置定时器
    esp_timer_create_args_t timer_args = {
        .callback = &timer_callback,
    };
    //创建定时器
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &TIMER));

    //启动MQTT任务
    MQTT_loop();
}