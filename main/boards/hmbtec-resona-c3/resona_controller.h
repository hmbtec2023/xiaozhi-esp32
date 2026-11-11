#pragma once
// HMB|TEC RESONA / AI V0.1.0 – ESP-IDF 5/6, ESP32-C3
// LD2410 UART 256000, PWM Flackerprozessor GPIO3, CALM 85%.
#include <cstdint>
#include <cstdio>
#include <algorithm>
#include <driver/uart.h>
#include <driver/ledc.h>
#include <esp_timer.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define RESONA_FLAME_PWM_GPIO GPIO_NUM_6

namespace resona {
struct Snapshot { bool presence=false, moving=false, still=false, calm=false; uint8_t level=0, pwm=0; bool greeting_pending=false, followup_pending=false; };
class Controller {
  uart_port_t port_=UART_NUM_1;
  float level_=0;
  bool presence_=false,moving_=false,still_=false,calm_=false, greeting_sent_=false,followup_sent_=false;
  uint8_t pwm_=0;
  int64_t last_frame_=0,last_update_=0,absence_since_=0,calm_since_=0,listen_since_=0;
  bool greeting_pending_=false,followup_pending_=false;
  uint8_t frame_[64]{}; size_t used_=0;
  static constexpr uint8_t max_pwm_=217; // 85% von 255
  static constexpr uint8_t base_pwm_=120;
  static constexpr int64_t us_per_ms_=1000;
  static void worker(void* arg) { auto* p=static_cast<Controller*>(arg); for(;;){p->tick();vTaskDelay(pdMS_TO_TICKS(30));} }
  void parse(uint8_t b, int64_t now) {
    const uint8_t head[]={0xF4,0xF3,0xF2,0xF1};
    if(used_<4){if(b==head[used_]) frame_[used_++]=b; else used_=(b==head[0]?1:0);return;}
    if(used_>=sizeof(frame_)){used_=0;return;}
    frame_[used_++]=b;
    if(used_==6){uint16_t len=frame_[4]|(frame_[5]<<8);if(len<5||len>48)used_=0;return;}
    if(used_<6)return;
    size_t expected=6+(frame_[4]|(frame_[5]<<8))+4;
    if(used_!=expected)return;
    if(frame_[expected-4]==0xF8&&frame_[expected-3]==0xF7&&frame_[expected-2]==0xF6&&frame_[expected-1]==0xF5 && frame_[6]==0x02 && frame_[7]==0xAA){
      uint8_t state=frame_[8];
      presence_=(state==1||state==2||state==3);
      moving_=(state==1||state==3);still_=(state==2||state==3);
      last_frame_=now;
    }
    used_=0;
  }
  void tick(){
    uint8_t rx[128];int n=uart_read_bytes(port_,rx,sizeof(rx),0);
    int64_t now=esp_timer_get_time();for(int i=0;i<n;i++)parse(rx[i],now);
    if(last_frame_==0||now-last_frame_>1500000){presence_=moving_=still_=false;}
    float dt=last_update_?std::min(0.25f,(now-last_update_)/1000000.0f):0.0f;last_update_=now;
    float rate=presence_&&still_&&!moving_?28.0f:!presence_?-35.0f: moving_&&!still_?-22.0f:-8.0f;
    level_=std::clamp(level_+rate*dt,0.0f,100.0f);
    if(!presence_){if(!absence_since_)absence_since_=now; if(now-absence_since_>=10000000){greeting_sent_=followup_sent_=false;greeting_pending_=followup_pending_=false;listen_since_=0;}}
    else absence_since_=0;
    if(!calm_ && presence_ && level_>=92.0f){calm_=true;calm_since_=now;}
    if(calm_ && (!presence_||level_<=88.0f)){calm_=false;calm_since_=0;}
    if(calm_&&calm_since_&&now-calm_since_>=3000000&&!greeting_sent_){greeting_pending_=true;greeting_sent_=true;ESP_LOGI("RESONA","CALM: Begruessung angefordert");}
    if(listen_since_&&!followup_sent_&&now-listen_since_>=60000000){followup_pending_=true;followup_sent_=true;listen_since_=0;}
    uint8_t target=0;
    if(presence_){target=base_pwm_;if(calm_){float t=std::clamp((level_-92.0f)/8.0f,0.0f,1.0f);target=static_cast<uint8_t>(base_pwm_+t*(max_pwm_-base_pwm_)+0.5f);}}
    if(pwm_<target)pwm_++;else if(pwm_>target)pwm_=static_cast<uint8_t>(std::max(0,static_cast<int>(pwm_)-2));
    ledc_set_duty(LEDC_LOW_SPEED_MODE,LEDC_CHANNEL_0,pwm_);ledc_update_duty(LEDC_LOW_SPEED_MODE,LEDC_CHANNEL_0);
  }
public:
  bool begin(){
    uart_config_t uc{};uc.baud_rate=256000;uc.data_bits=UART_DATA_8_BITS;uc.parity=UART_PARITY_DISABLE;uc.stop_bits=UART_STOP_BITS_1;uc.flow_ctrl=UART_HW_FLOWCTRL_DISABLE;uc.source_clk=UART_SCLK_DEFAULT;
    if(uart_driver_install(port_,2048,0,0,nullptr,0)!=ESP_OK)return false;
    if(uart_param_config(port_,&uc)!=ESP_OK)return false;
    if(uart_set_pin(port_,2,1,UART_PIN_NO_CHANGE,UART_PIN_NO_CHANGE)!=ESP_OK)return false;
    ledc_timer_config_t timer{};timer.speed_mode=LEDC_LOW_SPEED_MODE;timer.timer_num=LEDC_TIMER_0;timer.duty_resolution=LEDC_TIMER_8_BIT;timer.freq_hz=1000;timer.clk_cfg=LEDC_AUTO_CLK;
    if(ledc_timer_config(&timer)!=ESP_OK)return false;
    ledc_channel_config_t channel{};channel.gpio_num=RESONA_FLAME_PWM_GPIO;channel.speed_mode=LEDC_LOW_SPEED_MODE;channel.channel=LEDC_CHANNEL_0;channel.timer_sel=LEDC_TIMER_0;channel.duty=0;channel.hpoint=0;
    if(ledc_channel_config(&channel)!=ESP_OK)return false;
    return xTaskCreate(worker,"resona",4096,this,5,nullptr)==pdPASS;
  }
  Snapshot snapshot() const {return {presence_,moving_,still_,calm_,static_cast<uint8_t>(level_+0.5f),pwm_,greeting_pending_,followup_pending_};}
  // Aufruf NACH tatsaechlich abgeschlossener Begruessungs-TTS, nicht beim Start!
  void greeting_finished(){greeting_pending_=false;listen_since_=esp_timer_get_time();}
  void user_spoke(){listen_since_=0;followup_pending_=false;followup_sent_=true;}
  void followup_handled(){followup_pending_=false;}
};
} // namespace resona
