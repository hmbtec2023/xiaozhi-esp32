#include "wifi_board.h"
#include "codecs/no_audio_codec.h"
#include "display/display.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "led/single_led.h"
#include "hmb_dualeye.h"
#include <esp_log.h>
#define TAG "HmbDualEyeBoard"
class HmbDualEyeBoard : public WifiBoard {
private:
    NoDisplay display_;
    Button boot_button_;
#if HMB_DUALEYE_ENABLED
    HmbDualEye eyes_;
#endif
    void InitializeButtons(){
        boot_button_.OnClick([this](){
            auto& app=Application::GetInstance();
            if(app.GetDeviceState()==kDeviceStateStarting){EnterWifiConfigMode();return;}
            app.ToggleChatState();
        });
    }
public:
    HmbDualEyeBoard():boot_button_(BOOT_BUTTON_GPIO){
        InitializeButtons();
#if HMB_DUALEYE_ENABLED
        eyes_.Init();
        Application::GetInstance().RegisterStateChangeListener([this](DeviceState,DeviceState n){eyes_.SetState(n);});
        ESP_LOGI(TAG,"DualEye enabled");
#else
        ESP_LOGI(TAG,"DualEye disabled - XiaoZhi baseline test");
#endif
    }
    Led* GetLed() override {static SingleLed led(BUILTIN_LED_GPIO);return &led;}
    AudioCodec* GetAudioCodec() override {static NoAudioCodecSimplex c(AUDIO_INPUT_SAMPLE_RATE,AUDIO_OUTPUT_SAMPLE_RATE,AUDIO_I2S_SPK_GPIO_BCLK,AUDIO_I2S_SPK_GPIO_LRCK,AUDIO_I2S_SPK_GPIO_DOUT,AUDIO_I2S_MIC_GPIO_SCK,AUDIO_I2S_MIC_GPIO_WS,AUDIO_I2S_MIC_GPIO_DIN);return &c;}
    Display* GetDisplay() override {return &display_;}
};
DECLARE_BOARD(HmbDualEyeBoard);
