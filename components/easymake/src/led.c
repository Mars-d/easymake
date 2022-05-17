#include "easymake.h"
#include "led.h"
#include "driver/gpio.h"
#include <string.h>
#include "esp_log.h"

static int  led_status = 0;

void led_query(char* dat){
    char mdata[10];
    memset(mdata,0,10);
    sprintf(mdata,",\"led\":%d",led_status);
    strcat(dat,mdata);
}

void led_control(int setval){
    gpio_set_level(LED_PIN, setval);            // 把这个GPIO输出电平
    led_status = setval;
}

void Led_init(Sensor *Sensor_instance){
    sensor_register(led_query,led_control,"led",Sensor_instance);
    gpio_reset_pin(LED_PIN);
    gpio_set_direction(LED_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(LED_PIN, 0);
}