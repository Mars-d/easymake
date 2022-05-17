#include "easymake.h"
#include "led.h"
#include <stdio.h>
#include <string.h>
#include "esp_log.h"


int sensor_register(void (*cquery)(char*),void (*ccontrol)(int),char* cname, Sensor *cSensor_instance){
    for (int i = 0; i < Sensor_MAX; i++){
        if(cSensor_instance[i].flag == 0){
            cSensor_instance[i].flag = 1;
            strcpy(cSensor_instance[i].name,cname);
            cSensor_instance[i].query = cquery;
            cSensor_instance[i].control = ccontrol;
            return i;
        }
    }
    return -1;
}

void sensor_init(Sensor *cSensor_instance){
    for (int i = 0; i < Sensor_MAX; i++){
        cSensor_instance[i].flag = 0;
    }
    #ifdef LED 
    Led_init(cSensor_instance);
    #endif
}

int SensorQuery(char* updat, unsigned char rtype,Sensor *cSensor_instance){
    char dat[100];
    memset(dat,0,100);
    for (int i = 0; i < Sensor_MAX; i++){
        if(cSensor_instance[i].flag != 0){
            cSensor_instance[i].query(dat);
        }
    }
    return (sprintf(updat,"{\"id\":\"%s\",\"cmd\":%d%s}",SID,(int)rtype,dat));
}

int SensorControl(char* cname, int setval,Sensor *cSensor_instance){
    for (int i = 0; i < Sensor_MAX; i++){
        if(strcmp(cSensor_instance[i].name, cname)==0){
            cSensor_instance[i].control(setval);
            return 1;
        }
    }
    return 0;
}