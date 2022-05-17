#ifndef _EASYMAKE_H_
#define _EASYMAKE_H_
#define LED

#define CID "p0000000001"
#define SID "p0000000002"
#define SUB "p0000000002"
#define PUB "wifi/p0000000001"

#define ESP_WIFI_SSID      "ceshi"
#define ESP_WIFI_PASS      "11111111"
#define ESP_MAXIMUM_RETRY  5

#define CONFIG_BROKER_URL "mqtt://100.100.100.100"
#define CONFIG_PORT 1883

#define AUTO_UPLOAD_TIME 10
#define Sensor_MAX 5


typedef struct{
    int flag;
    char name[10];
    void (*query)(char*);
    void (*control)(int);
}Sensor;

int sensor_register(void (*cquery)(char*),void (*ccontrol)(int),char* cname, Sensor *cSensor_instance);
void sensor_init(Sensor *cSensor_instance);
int SensorQuery(char* updat, unsigned char rtype,Sensor *cSensor_instance);
int SensorControl(char* cname, int setval,Sensor *cSensor_instance);

#endif