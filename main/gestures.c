#include "sdkconfig.h"
#ifdef CONFIG_SATELLITE_GESTURES
#include "gestures.h"


#include "gesture-filter.h"
#include "paj-registers.h"
#include "state.h"
#include "link.h"

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>

static i2c_master_dev_handle_t sensor;
static bool ready;
static esp_err_t init_error=ESP_ERR_INVALID_STATE;
static portMUX_TYPE lock=portMUX_INITIALIZER_UNLOCKED;
static struct { uint32_t count, errors, ambiguous; uint16_t raw, observed; int index; int64_t at; } stats={.index=-1};
static const char *names[]={"up","down","left","right","forward","backward","clockwise","counterclockwise","wave"};
static esp_err_t write_reg(uint8_t reg, uint8_t value) {
    uint8_t data[]={reg,value}; return i2c_master_transmit(sensor,data,2,50);
}
static esp_err_t read_reg(uint8_t reg,uint8_t *data,size_t size) {
    return i2c_master_transmit_receive(sensor,&reg,1,data,size,50);
}
void gestures_init(i2c_master_bus_handle_t bus) {
    vTaskDelay(pdMS_TO_TICKS(100));
    i2c_device_config_t cfg={.dev_addr_length=I2C_ADDR_BIT_LEN_7,.device_address=0x73,.scl_speed_hz=100000};
    init_error=i2c_master_bus_add_device(bus,&cfg,&sensor);
    if (init_error!=ESP_OK) return;
    init_error=write_reg(0xef,0);
    vTaskDelay(pdMS_TO_TICKS(50));
    // A first access may wake the sensor. Retry bank selection once.
    if (init_error!=ESP_OK) init_error=write_reg(0xef,0);
    uint8_t id[2]={0};
    if (init_error==ESP_OK) init_error=read_reg(0,id,2);
    if (init_error==ESP_OK && (id[0]!=0x20 || id[1]!=0x76)) init_error=ESP_ERR_INVALID_RESPONSE;
    for (size_t i=0;init_error==ESP_OK && i<sizeof(paj_registers)/sizeof(paj_registers[0]);i++)
        init_error=write_reg(paj_registers[i][0],paj_registers[i][1]);
    if (init_error==ESP_OK) init_error=write_reg(0xef,1);
    if (init_error==ESP_OK) init_error=write_reg(0x65,18); // Seeed NEAR_240FPS
    if (init_error==ESP_OK) init_error=write_reg(0xef,0);
    ready=init_error==ESP_OK;
    if (!ready) { i2c_master_bus_rm_device(sensor); sensor=NULL; }
}
bool gestures_ready(void) { return ready; }
static void poll_task(void *arg) {
    gesture_filter_t filter={0};
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(50));
#ifdef CONFIG_SATELLITE_OTA
        if (satellite.updating) { filter=(gesture_filter_t){0}; continue; }
#endif
        if (!satellite.online) { filter=(gesture_filter_t){0}; continue; }
        uint8_t flags[2]={0};
        esp_err_t err=read_reg(0x43,flags,2);
        int64_t now=esp_timer_get_time();
        if (err!=ESP_OK) {
            portENTER_CRITICAL(&lock); stats.errors++; portEXIT_CRITICAL(&lock);
            filter=(gesture_filter_t){0};
            vTaskDelay(pdMS_TO_TICKS(200)); continue;
        }
        unsigned rejected=filter.rejected;
        unsigned raw=gesture_filter_feed(&filter,((unsigned)flags[1]<<8)|flags[0],now/1000);
        if (filter.rejected!=rejected) {
            portENTER_CRITICAL(&lock); stats.ambiguous++; portEXIT_CRITICAL(&lock);
        }
        if (!raw) continue;
        int index=0; while ((1u<<index)!=raw) index++;
        portENTER_CRITICAL(&lock);
        stats.count++; stats.raw=raw; stats.observed=filter.last_flags; stats.index=index; stats.at=now/1000;
        portEXIT_CRITICAL(&lock);
        // Disconnected events are never queued or replayed on reconnection.
        if (satellite.online) link_send_gesture(names[index]);
    }
}
void gestures_start(void) {
    if (ready && xTaskCreate(poll_task,"gestures",4096,NULL,2,NULL)!=pdPASS) { ready=false; init_error=ESP_ERR_NO_MEM; }
}

#endif
