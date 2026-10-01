#include "wifi_board.h"
#include "codecs/es8311_audio_codec.h"
#include "display/lcd_display.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "led/single_led.h"
#include "assets/lang_config.h"
#include <esp_log.h>
#include <esp_efuse_table.h>
#include <driver/i2c_master.h>

#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_gc9a01.h>
#include "system_reset.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include <esp_timer.h>
#include <esp_random.h>
#include "i2c_device.h"
#include <esp_lcd_panel_vendor.h>
#include <driver/spi_common.h>
#include "power_save_timer.h"
#include <esp_sleep.h>
#include <driver/rtc_io.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "power_manager.h"

#include <driver/sdmmc_host.h>
#include <esp_vfs_fat.h>
#include <sdmmc_cmd.h>
#include <cstdio>
#include "application.h"
#include <string>
#include <dirent.h>
#include "mcp_server.h"

#include <ctime>
#include <cmath>
#include <cstring>

#define TAG "Spotpear_ESP32_S3_1_28_BOX"

LV_FONT_DECLARE(font_noto_sans_basic_16_4);
LV_FONT_DECLARE(font_material_symbols_16_4);


class Cst816d : public I2cDevice {
public:
    struct TouchPoint_t {
        int num = 0;
        int x = -1;
        int y = -1;
    };
    Cst816d(i2c_master_bus_handle_t i2c_bus, uint8_t addr) : I2cDevice(i2c_bus, addr) {
        uint8_t chip_id = ReadReg(0xA3);
        ESP_LOGI(TAG, "Get chip ID: 0x%02X", chip_id);
        last_chip_id_ = chip_id;
        read_buffer_ = new uint8_t[6];
    }

    ~Cst816d() {
        if (read_buffer_) {
            delete[] read_buffer_;
            read_buffer_ = nullptr;
        }
    }

    void UpdateTouchPoint() {
        if (!read_buffer_) return;
        ReadRegs(0x02, read_buffer_, 6);
        if (read_buffer_[0] == 0xFF) {
            read_buffer_[0] = 0x00;
        }
        tp_.num = read_buffer_[0] & 0x01;
        tp_.x = ((read_buffer_[1] & 0x0F) << 8) | read_buffer_[2];
        tp_.y = ((read_buffer_[3] & 0x0F) << 8) | read_buffer_[4];
    }

    const TouchPoint_t& GetTouchPoint() const {
        return tp_;
    }

    static bool Probe(i2c_master_bus_handle_t i2c_bus, uint8_t addr, uint8_t& chip_id) {
        if (!i2c_bus) return false;
        i2c_master_dev_handle_t dev = nullptr;
        i2c_device_config_t cfg = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = addr,
            .scl_speed_hz = 400 * 1000,
            .scl_wait_us = 0,
            .flags = {
                .disable_ack_check = 0,
            },
        };
        esp_err_t ret = i2c_master_bus_add_device(i2c_bus, &cfg, &dev);
        if (ret != ESP_OK || dev == nullptr) {
            return false;
        }
        uint8_t reg = 0xA3;
        uint8_t id = 0;
        ret = i2c_master_transmit_receive(dev, &reg, 1, &id, 1, 100);
        i2c_master_bus_rm_device(dev);
        if (ret == ESP_OK) {
            chip_id = id;
            return true;
        }
        return false;
    }

private:
    uint8_t* read_buffer_ = nullptr;
    TouchPoint_t tp_;
    uint8_t last_chip_id_ = 0;
};


class CustomLcdDisplay : public SpiLcdDisplay {
private:
    lv_obj_t* light_root_=nullptr;
    lv_obj_t* clock_label_=nullptr;
    lv_obj_t* mode_label_=nullptr;
    lv_obj_t* light_ring_outer_=nullptr;
    lv_obj_t* light_ring_inner_=nullptr;
    lv_obj_t* light_core_=nullptr;
    lv_timer_t* light_timer_=nullptr;
    int64_t last_clock_update_ms_=0;
    int64_t last_status_update_ms_=0;
    float phase_=0.0f;

    static constexpr int LIGHT_CX=120;
    static constexpr int LIGHT_CY=126;
    static constexpr int CORE_BASE=30;
    static constexpr int RING_INNER_BASE=72;
    static constexpr int RING_OUTER_BASE=122;

    void UpdateClock(){
        if(!clock_label_) return;
        time_t now=time(nullptr);
        struct tm timeinfo;
        if(!localtime_r(&now,&timeinfo)) return;
        char buffer[6];
        strftime(buffer,sizeof(buffer),"%H:%M",&timeinfo);
        lv_label_set_text(clock_label_,buffer);
        lv_obj_move_foreground(clock_label_);
    }

    bool IsClockText(const char* text){
        if(!text || !text[0]) return false;
        int hour=-1;
        int minute=-1;
        char tail=0;
        int matched=sscanf(text,"%d:%d%c",&hour,&minute,&tail);
        return matched==2 && hour>=0 && hour<=23 && minute>=0 && minute<=59;
    }

    void UpdateModeStatus(){
        if(!mode_label_ || !status_label_) return;
        const char* text=lv_label_get_text(status_label_);
        if(!text || !text[0] || IsClockText(text)) return;
        const char* current=lv_label_get_text(mode_label_);
        if(!current || strcmp(current,text)!=0) lv_label_set_text(mode_label_,text);
        lv_obj_move_foreground(mode_label_);
    }

    static void SetFilledCircle(lv_obj_t* obj,int size,lv_color_t color,lv_opa_t opa){
        if(!obj) return;
        lv_obj_set_size(obj,size,size);
        lv_obj_set_style_radius(obj,LV_RADIUS_CIRCLE,0);
        lv_obj_set_style_bg_color(obj,color,0);
        lv_obj_set_style_bg_opa(obj,opa,0);
        lv_obj_set_style_border_width(obj,0,0);
        lv_obj_set_style_pad_all(obj,0,0);
        lv_obj_clear_flag(obj,LV_OBJ_FLAG_SCROLLABLE);
    }

    static void SetRing(lv_obj_t* obj,int size,int width,lv_color_t color,lv_opa_t opa){
        if(!obj) return;
        lv_obj_set_size(obj,size,size);
        lv_obj_set_style_radius(obj,LV_RADIUS_CIRCLE,0);
        lv_obj_set_style_bg_opa(obj,LV_OPA_TRANSP,0);
        lv_obj_set_style_border_color(obj,color,0);
        lv_obj_set_style_border_opa(obj,opa,0);
        lv_obj_set_style_border_width(obj,width,0);
        lv_obj_set_style_pad_all(obj,0,0);
        lv_obj_clear_flag(obj,LV_OBJ_FLAG_SCROLLABLE);
    }

    static void CenterObject(lv_obj_t* obj,int size){
        if(!obj) return;
        lv_obj_set_size(obj,size,size);
        lv_obj_set_pos(obj,LIGHT_CX-size/2,LIGHT_CY-size/2);
    }

    void AnimateLight(){
        if(!light_root_ || !light_core_ || !light_ring_inner_ || !light_ring_outer_) return;
        int64_t now=esp_timer_get_time()/1000;
        if(now-last_clock_update_ms_>=1000){
            last_clock_update_ms_=now;
            UpdateClock();
        }
        if(now-last_status_update_ms_>=200){
            last_status_update_ms_=now;
            UpdateModeStatus();
        }

        DeviceState state=Application::GetInstance().GetDeviceState();
        float speed=0.045f;
        float amplitude=1.0f;
        int core_base=CORE_BASE;
        int inner_base=RING_INNER_BASE;
        int outer_base=RING_OUTER_BASE;
        lv_opa_t core_opa=LV_OPA_COVER;
        lv_opa_t inner_opa=LV_OPA_40;
        lv_opa_t outer_opa=LV_OPA_20;

        if(state==kDeviceStateListening){
            speed=0.075f;
            amplitude=1.5f;
            core_base=36;
            inner_base=66;
            outer_base=112;
            inner_opa=LV_OPA_60;
            outer_opa=LV_OPA_30;
        }else if(state==kDeviceStateSpeaking){
            speed=0.16f;
            amplitude=2.4f;
            core_base=34;
            inner_base=76;
            outer_base=126;
            inner_opa=LV_OPA_70;
            outer_opa=LV_OPA_40;
        }

        phase_+=speed;
        if(phase_>6.2831853f) phase_-=6.2831853f;
        float wave=(sinf(phase_)+1.0f)*0.5f;
        int core_size=core_base+(int)(wave*6.0f*amplitude);
        int inner_size=inner_base+(int)(wave*8.0f*amplitude);
        int outer_size=outer_base+(int)(wave*10.0f*amplitude);

        CenterObject(light_core_,core_size);
        CenterObject(light_ring_inner_,inner_size);
        CenterObject(light_ring_outer_,outer_size);
        lv_obj_set_style_bg_opa(light_core_,core_opa,0);
        lv_obj_set_style_border_opa(light_ring_inner_,inner_opa,0);
        lv_obj_set_style_border_opa(light_ring_outer_,outer_opa,0);
        lv_obj_move_foreground(clock_label_);
        lv_obj_move_foreground(mode_label_);
    }

    static void LightTimerCallback(lv_timer_t* timer){
        auto* self=static_cast<CustomLcdDisplay*>(lv_timer_get_user_data(timer));
        if(self) self->AnimateLight();
    }

    static void SetupLightAsync(void* user_data){
        auto* self=static_cast<CustomLcdDisplay*>(user_data);
        if(self) self->SetupLight();
    }

    void SetupLight(){
        if(light_root_){
            ESP_LOGW(TAG,"SetupLight ignored: Lichtblick already initialized");
            return;
        }
        ESP_LOGI(TAG,"SetupLight start");
        lv_obj_t* screen=lv_screen_active();
        if(!screen){
            ESP_LOGE(TAG,"SetupLight failed: no active LVGL screen");
            return;
        }

        light_root_=lv_obj_create(screen);
        if(!light_root_){
            ESP_LOGE(TAG,"SetupLight failed: light_root creation failed");
            return;
        }
        lv_obj_set_size(light_root_,240,240);
        lv_obj_set_pos(light_root_,0,0);
        lv_obj_set_style_bg_color(light_root_,lv_color_black(),0);
        lv_obj_set_style_bg_opa(light_root_,LV_OPA_COVER,0);
        lv_obj_set_style_border_width(light_root_,0,0);
        lv_obj_set_style_pad_all(light_root_,0,0);
        lv_obj_set_style_radius(light_root_,0,0);
        lv_obj_clear_flag(light_root_,LV_OBJ_FLAG_SCROLLABLE);

        // HMB | TEC Clock
        clock_label_=lv_label_create(light_root_);
        lv_label_set_text(clock_label_,"--:--");
        lv_obj_set_style_text_color(clock_label_,lv_color_white(),0);
        lv_obj_set_style_text_font(clock_label_,&lv_font_montserrat_24,0);
        lv_obj_set_style_text_opa(clock_label_,LV_OPA_COVER,0);
        lv_obj_set_style_bg_opa(clock_label_,LV_OPA_TRANSP,0);
        lv_obj_set_style_pad_all(clock_label_,0,0);
        lv_obj_align(clock_label_,LV_ALIGN_TOP_MID,0,6);

        // HMB | TEC Lichtblick symbol
        light_ring_outer_=lv_obj_create(light_root_);
        light_ring_inner_=lv_obj_create(light_root_);
        light_core_=lv_obj_create(light_root_);
        SetRing(light_ring_outer_,RING_OUTER_BASE,2,lv_color_amber(),LV_OPA_20);
        SetRing(light_ring_inner_,RING_INNER_BASE,3,lv_color_amber(),LV_OPA_40);
        SetFilledCircle(light_core_,CORE_BASE,lv_color_amber(),LV_OPA_COVER);
        CenterObject(light_ring_outer_,RING_OUTER_BASE);
        CenterObject(light_ring_inner_,RING_INNER_BASE);
        CenterObject(light_core_,CORE_BASE);

        // HMB | TEC Operating Status
        mode_label_=lv_label_create(light_root_);
        lv_label_set_text(mode_label_,"BEREIT");
        lv_obj_set_style_text_color(mode_label_,lv_color_white(),0);
        lv_obj_set_style_text_font(mode_label_,&font_noto_sans_basic_16_4,0);
        lv_obj_set_style_text_opa(mode_label_,LV_OPA_COVER,0);
        lv_obj_set_style_bg_opa(mode_label_,LV_OPA_TRANSP,0);
        lv_obj_set_style_pad_all(mode_label_,0,0);
        lv_obj_set_width(mode_label_,220);
        lv_obj_set_style_text_align(mode_label_,LV_TEXT_ALIGN_CENTER,0);
        lv_obj_align(mode_label_,LV_ALIGN_BOTTOM_MID,0,-6);

        UpdateClock();
        UpdateModeStatus();
        lv_obj_move_foreground(clock_label_);
        lv_obj_move_foreground(mode_label_);

        if(!light_timer_) light_timer_=lv_timer_create(LightTimerCallback,45,this);
        ESP_LOGI(TAG,"SetupLight complete");
        ESP_LOGI(TAG,"HMB|TEC Lichtblick active");
    }

public:
    CustomLcdDisplay(esp_lcd_panel_io_handle_t io_handle,
                     esp_lcd_panel_handle_t panel_handle,
                     int width,
                     int height,
                     int offset_x,
                     int offset_y,
                     bool mirror_x,
                     bool mirror_y,
                     bool swap_xy)
        : SpiLcdDisplay(io_handle,panel_handle,width,height,offset_x,offset_y,mirror_x,mirror_y,swap_xy){}

    ~CustomLcdDisplay(){
        if(light_timer_){
            lv_timer_delete(light_timer_);
            light_timer_=nullptr;
        }
    }

    virtual void SetupUI() override{
        SpiLcdDisplay::SetupUI();
        {
            DisplayLockGuard lock(this);
            if(status_bar_){
                lv_obj_set_style_pad_left(status_bar_,LV_HOR_RES*0.33,0);
                lv_obj_set_style_pad_right(status_bar_,LV_HOR_RES*0.33,0);
            }
        }
#if HMB_SINGLE_EYE_ENABLED
        ESP_LOGI(TAG,"Scheduling HMB|TEC Lichtblick setup");
        lv_async_call(SetupLightAsync,this);
#endif
    }
};

class Spotpear_ESP32_S3_1_28_BOX : public WifiBoard {
private:
    i2c_master_bus_handle_t codec_i2c_bus_ = nullptr;
    i2c_master_bus_handle_t i2c_bus_ = nullptr;
    Button boot_button_;
    Display* display_ = nullptr;
    esp_timer_handle_t touchpad_timer_ = nullptr;
    Cst816d* cst816d_ = nullptr;
    PowerSaveTimer* power_save_timer_ = nullptr;
    esp_lcd_panel_handle_t panel_ = nullptr;
    PowerManager* power_manager_ = nullptr;
    sdmmc_card_t* sd_card_ = nullptr;

    void InitializeSdCard(){
        ESP_LOGI(TAG, "Initializing onboard microSD");

        sdmmc_host_t host = SDMMC_HOST_DEFAULT();
        host.slot = SDMMC_HOST_SLOT_1;
        host.max_freq_khz = SDMMC_FREQ_DEFAULT;

        sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
        slot.width = 1;
        slot.clk = SDMMC_CLK_PIN;
        slot.cmd = SDMMC_CMD_PIN;
        slot.d0 = SDMMC_D0_PIN;
        slot.d1 = GPIO_NUM_NC;
        slot.d2 = GPIO_NUM_NC;
        slot.d3 = GPIO_NUM_NC;
        slot.cd = SDMMC_SLOT_NO_CD;
        slot.wp = SDMMC_SLOT_NO_WP;

        const esp_vfs_fat_sdmmc_mount_config_t mount_config = {
            .format_if_mount_failed = false,
            .max_files = 5,
            .allocation_unit_size = 16 * 1024,
        };

        esp_err_t ret = esp_vfs_fat_sdmmc_mount(SD_MOUNT_POINT, &host, &slot, &mount_config, &sd_card_);
        if(ret != ESP_OK){
            ESP_LOGE(TAG, "microSD mount failed: %s", esp_err_to_name(ret));
            sd_card_ = nullptr;
            return;
        }

        ESP_LOGI(TAG, "microSD mounted: %s", SD_MOUNT_POINT);

        FILE* test_file = fopen(SD_MOUNT_POINT "/audio/test1.ogg", "rb");
        if(test_file){
            fseek(test_file, 0, SEEK_END);
            long size = ftell(test_file);
            fclose(test_file);
            ESP_LOGI(TAG, "Found /audio/test1.ogg, size: %ld bytes", size);
        }else{
            ESP_LOGW(TAG, "/audio/test1.ogg not found");
        }

        ESP_LOGI(TAG, "microSD: %s, capacity: %llu MB",
            sd_card_->cid.name,
            (unsigned long long)(sd_card_->csd.capacity * sd_card_->csd.sector_size / (1024ULL * 1024ULL)));
    }

    bool PlayOggFromSd(const char* path){
        ESP_LOGI(TAG,"Loading OGG: %s",path);

        FILE* file=fopen(path,"rb");
        if(!file){
            ESP_LOGE(TAG,"Cannot open OGG: %s",path);
            return false;
        }

        fseek(file,0,SEEK_END);
        long file_size=ftell(file);
        fseek(file,0,SEEK_SET);

        if(file_size<=0){
            ESP_LOGE(TAG,"Invalid OGG size: %ld",file_size);
            fclose(file);
            return false;
        }

        ESP_LOGI(TAG,"OGG size: %ld bytes",file_size);

        uint8_t* ogg=static_cast<uint8_t*>(
            heap_caps_malloc(file_size,MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
        );

        if(!ogg){
            ESP_LOGE(TAG,"PSRAM allocation failed: %ld bytes",file_size);
            fclose(file);
            return false;
        }

        size_t bytes_read=fread(ogg,1,file_size,file);
        fclose(file);

        if(bytes_read!=static_cast<size_t>(file_size)){
            ESP_LOGE(TAG,"OGG read failed: %u/%ld bytes",(unsigned)bytes_read,file_size);
            heap_caps_free(ogg);
            return false;
        }

        ESP_LOGI(TAG,"OGG loaded in PSRAM, starting playback");

        std::string_view ogg_view(
            reinterpret_cast<const char*>(ogg),
            static_cast<size_t>(file_size)
        );

        Application::GetInstance().GetAudioService().PlaySound(ogg_view);

        heap_caps_free(ogg);

        ESP_LOGI(TAG,"OGG submitted to AudioService");
        return true;
    }
    void StopSdAudio(){
        ESP_LOGI(TAG, "Stopping SD audio");
        Application::GetInstance().GetAudioService().ResetDecoder();
    }

    std::string ListSdAudioFiles(){
        ESP_LOGI(TAG, "Audio files on SD:");

        DIR* dir=opendir(SD_MOUNT_POINT "/audio");
        if(!dir){
            ESP_LOGE(TAG, "Cannot open " SD_MOUNT_POINT "/audio");
            return "SD audio directory cannot be opened.";
        }

        struct dirent* entry;
        int count=0;
        std::string result;

        while((entry=readdir(dir))!=nullptr){
            if(entry->d_name[0]=='.'){
                continue;
            }

            std::string filename=entry->d_name;
            if(filename.size()<4 || filename.substr(filename.size()-4)!=".ogg"){
                continue;
            }

            ESP_LOGI(TAG, "  [%d] %s",++count,entry->d_name);

            if(!result.empty()){
                result+=", ";
            }
            result+=filename;
        }

        closedir(dir);

        ESP_LOGI(TAG, "Audio files found: %d",count);

        if(count==0){
            return "No OGG audio files found.";
        }

        return result;
    }
    
    void InitializePowerSaveTimer() {
        rtc_gpio_init(GPIO_NUM_3);
        rtc_gpio_set_direction(GPIO_NUM_3, RTC_GPIO_MODE_OUTPUT_ONLY);
        rtc_gpio_set_level(GPIO_NUM_3, 1);

        power_save_timer_ = new PowerSaveTimer(-1, 60, 290);
        power_save_timer_->OnEnterSleepMode([this]() {
            GetDisplay()->SetPowerSaveMode(true);
            GetBacklight()->SetBrightness(1);
        });
        power_save_timer_->OnExitSleepMode([this]() {
            GetDisplay()->SetPowerSaveMode(false);
            GetBacklight()->RestoreBrightness();
        });
        power_save_timer_->OnShutdownRequest([this]() {
            ESP_LOGI(TAG, "Shutting down");
            // 关闭ES8311音频编解码器
            auto codec = GetAudioCodec();
            if (codec) {
                codec->EnableInput(false);
                codec->EnableOutput(false);
            }
            rtc_gpio_set_level(GPIO_NUM_3, 0);
            // 启用保持功能，确保睡眠期间电平不变
            rtc_gpio_hold_en(GPIO_NUM_3);
            esp_lcd_panel_disp_on_off(panel_, false); //关闭显示
            esp_deep_sleep_start();
        });
        power_save_timer_->SetEnabled(true);
    }

    void InitializePowerManager() {
        power_manager_ = new PowerManager(BATTERY_CHARGING_PIN, ADC_CHANNEL_0);
        power_manager_->OnChargingStatusChanged([this](bool is_charging) {
            if (is_charging) {
                power_save_timer_->SetEnabled(false);
            } else {
                power_save_timer_->SetEnabled(true);
            }
        });
    }

    void InitializeCodecI2c() {
        // Initialize I2C peripheral
        i2c_master_bus_config_t i2c_bus_cfg = {
            .i2c_port = I2C_NUM_0,
            .sda_io_num = AUDIO_CODEC_I2C_SDA_PIN,
            .scl_io_num = AUDIO_CODEC_I2C_SCL_PIN,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            // .glitch_ignore_cnt = 7,
            // .intr_priority = 0,
            // .trans_queue_depth = 0,
            // .flags = {
            //     .enable_internal_pullup = 1,
            // },
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_cfg, &codec_i2c_bus_));
    }

    void InitializeCodecI2c_Touch() {
        // Initialize I2C peripheral
        i2c_master_bus_config_t i2c_bus_cfg = {
            .i2c_port = I2C_NUM_1,
            .sda_io_num = TP_PIN_NUM_TP_SDA,
            .scl_io_num = TP_PIN_NUM_TP_SCL,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags = {
                .enable_internal_pullup = 1,
            },
        };
        esp_err_t ret = i2c_new_master_bus(&i2c_bus_cfg, &i2c_bus_);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "i2c_new_master_bus failed: %s", esp_err_to_name(ret));
            i2c_bus_ = nullptr;
        }
    }


    static void touchpad_timer_callback(void* arg) {
        auto* board = static_cast<Spotpear_ESP32_S3_1_28_BOX*>(arg);
        if (!board || !board->cst816d_) return;
        static bool was_touched = false;
        static int64_t touch_start_time = 0;
        const int64_t TOUCH_THRESHOLD_MS = 500;  // 触摸时长阈值，超过500ms视为长按

        board->cst816d_->UpdateTouchPoint();
        auto touch_point = board->cst816d_->GetTouchPoint();

        // 检测触摸开始
        if (touch_point.num > 0 && !was_touched) {
            was_touched = true;
            touch_start_time = esp_timer_get_time() / 1000; // 转换为毫秒
        }
        // 检测触摸释放
        else if (touch_point.num == 0 && was_touched) {
            was_touched = false;
            int64_t touch_duration = (esp_timer_get_time() / 1000) - touch_start_time;

            // 只有短触才触发
            if (touch_duration < TOUCH_THRESHOLD_MS) {
                auto& app = Application::GetInstance();
                // During startup (before connected), pressing touch enters Wi-Fi config mode without reboot
                if (app.GetDeviceState() == kDeviceStateStarting) {
                    board->EnterWifiConfigMode();
                    return;
                }
                app.ToggleChatState();
            }
        }
    }

    void InitializeCst816DTouchPad() {
        ESP_LOGI(TAG, "Init Cst816D");

        // RST/INT 管脚初始化
        gpio_config_t io_conf = {};
        io_conf.intr_type = GPIO_INTR_DISABLE;
        io_conf.mode = GPIO_MODE_OUTPUT;
        io_conf.pin_bit_mask = (1ULL << TP_PIN_NUM_TP_RST);
        io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
        io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
        gpio_config(&io_conf);

        gpio_config_t int_conf = {};
        int_conf.intr_type = GPIO_INTR_DISABLE;
        int_conf.mode = GPIO_MODE_INPUT;
        int_conf.pin_bit_mask = (1ULL << TP_PIN_NUM_TP_INT);
        int_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
        int_conf.pull_up_en = GPIO_PULLUP_ENABLE;
        gpio_config(&int_conf);

        // 触摸芯片复位序列
        gpio_set_level(TP_PIN_NUM_TP_RST, 0);
        vTaskDelay(pdMS_TO_TICKS(5));
        gpio_set_level(TP_PIN_NUM_TP_RST, 1);
        vTaskDelay(pdMS_TO_TICKS(50));

        uint8_t chip_id = 0;
        if (!i2c_bus_) {
            ESP_LOGW(TAG, "Touch I2C bus not initialized, skip touch");
            return;
        }
        bool touch_available = Cst816d::Probe(i2c_bus_, 0x15, chip_id);
        if (!touch_available) {
            ESP_LOGW(TAG, "CST816D not found, running in non-touch mode");
            i2c_del_master_bus(i2c_bus_);
            i2c_bus_ = nullptr;
            return;
        }

        cst816d_ = new Cst816d(i2c_bus_, 0x15);

        esp_timer_create_args_t timer_args = {
            .callback = touchpad_timer_callback,
            .arg = this,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "touchpad_timer",
            .skip_unhandled_events = true,
        };

        if (esp_timer_create(&timer_args, &touchpad_timer_) == ESP_OK) {
            esp_timer_start_periodic(touchpad_timer_, 10 * 1000); // 10ms = 10000us
        }
    }

    void InitializeSpi() {
        ESP_LOGI(TAG, "Initialize SPI bus");
        spi_bus_config_t buscfg = GC9A01_PANEL_BUS_SPI_CONFIG(DISPLAY_SPI_SCLK_PIN, DISPLAY_SPI_MOSI_PIN,
                                    DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t));
        ESP_ERROR_CHECK(spi_bus_initialize(SPI3_HOST, &buscfg, SPI_DMA_CH_AUTO));
    }

    void InitializeGc9a01Display() {
        ESP_LOGI(TAG, "Init GC9A01 display");
        ESP_LOGI(TAG, "Install panel IO");
        esp_lcd_panel_io_handle_t io_handle = NULL;
        esp_lcd_panel_io_spi_config_t io_config = GC9A01_PANEL_IO_SPI_CONFIG(DISPLAY_SPI_CS_PIN, DISPLAY_SPI_DC_PIN, 0, NULL);
        io_config.pclk_hz = DISPLAY_SPI_SCLK_HZ;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI3_HOST, &io_config, &io_handle));

        ESP_LOGI(TAG, "Install GC9A01 panel driver");
        esp_lcd_panel_handle_t panel_handle = NULL;
        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = DISPLAY_SPI_RESET_PIN;    // Set to -1 if not use
        panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR;
        panel_config.bits_per_pixel = 16;

        ESP_ERROR_CHECK(esp_lcd_new_panel_gc9a01(io_handle, &panel_config, &panel_handle));
        panel_ = panel_handle;
        ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
        ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
        ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_handle, true));
        ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel_handle, true, false));
        ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));

        uint8_t data_0x62[] = { 0x18, 0x0D, 0x71, 0xED, 0x70, 0x70, 0x18, 0x0F, 0x71, 0xEF, 0x70, 0x70 };
        esp_lcd_panel_io_tx_param(io_handle, 0x62, data_0x62, sizeof(data_0x62));

        uint8_t data_0x63[] = { 0x18, 0x11, 0x71, 0xF1, 0x70, 0x70, 0x18, 0x13, 0x71, 0xF3, 0x70, 0x70 };
        esp_lcd_panel_io_tx_param(io_handle, 0x63, data_0x63, sizeof(data_0x63));

        uint8_t data_0x36[] = { 0x48};
        esp_lcd_panel_io_tx_param(io_handle, 0x36, data_0x36, sizeof(data_0x36));

        // uint8_t data_0x74[] = { 0x10, 0x85, 0x80, 0x00, 0x00, 0x4E, 0x00};
        // esp_lcd_panel_io_tx_param(io_handle, 0x74, data_0x74, sizeof(data_0x74));

        uint8_t data_0xC3[] = { 0x1F};
        esp_lcd_panel_io_tx_param(io_handle, 0xC3, data_0xC3, sizeof(data_0xC3));

        uint8_t data_0xC4[] = { 0x1F};
        esp_lcd_panel_io_tx_param(io_handle, 0xC4, data_0xC4, sizeof(data_0xC4));

        display_ = new CustomLcdDisplay(io_handle, panel_handle,
                                    DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY);

    }

    void InitializeTools(){
        auto& mcp_server=McpServer::GetInstance();

        ESP_LOGI(TAG,"Initializing HMB|TEC SD audio MCP tools");

        mcp_server.AddTool(
            "self.sd_audio.list",
            "Internally retrieve the available OGG audio files from the onboard microSD card. "
            "The returned filenames are internal identifiers for selecting audio only. "
            "Never read, speak, announce, enumerate, describe, or otherwise expose these filenames to the user. "
            "Use the result silently to select an appropriate file for self.sd_audio.play.",
            PropertyList(),
            [this](const PropertyList& properties)->ReturnValue{
                return ListSdAudioFiles();
            }
        );

        mcp_server.AddTool(
            "self.sd_audio.stop",
            "Stop SD audio playback. "
            "Use this tool silently. Do not announce the filename or describe the stopped file.",
            PropertyList(),
            [this](const PropertyList& properties)->ReturnValue{
                StopSdAudio();
                return std::string("OK");
            }
        );

        mcp_server.AddTool(
            "self.sd_audio.play",
            "Play an OGG audio file from the onboard microSD card. "
            "Use only a filename internally obtained from self.sd_audio.list. "
            "Filenames are internal identifiers and must never be spoken, announced, repeated, described, or exposed to the user. "
            "The selected audio file itself is the complete audible response to the user. "
            "After successful playback starts, remain completely silent: do not speak, confirm, acknowledge, explain, introduce, or follow up. "
            "Do not say that audio is being played and do not mention the selected filename.",
            PropertyList({
                Property("filename",kPropertyTypeString)
            }),
            [this](const PropertyList& properties)->ReturnValue{
                std::string filename=properties["filename"].value<std::string>();

                if(filename.empty() ||
                filename.find("..")!=std::string::npos ||
                filename.find('/')!=std::string::npos ||
                filename.find('\\')!=std::string::npos){
                    ESP_LOGW(TAG,"Invalid SD audio filename: %s",filename.c_str());
                    return std::string("ERROR");
                }

                if(filename.size()<4 || filename.substr(filename.size()-4)!=".ogg"){
                    ESP_LOGW(TAG,"Not an OGG file: %s",filename.c_str());
                    return std::string("ERROR");
                }

                std::string path=SD_MOUNT_POINT "/audio/"+filename;

                if(!PlayOggFromSd(path.c_str())){
                    ESP_LOGE(TAG,"SD audio playback failed: %s",filename.c_str());
                    return std::string("ERROR");
                }

                return std::string("OK");
            }
        );
    }

    void InitializeButtons() {
        boot_button_.OnClick([this]() {
            auto& app = Application::GetInstance();
            // During startup (before connected), pressing BOOT button enters Wi-Fi config mode without reboot
            if (app.GetDeviceState() == kDeviceStateStarting) {
                EnterWifiConfigMode();
                return;
            }
            app.ToggleChatState();
        });
        boot_button_.OnLongPress([this](){
            auto& audio=Application::GetInstance().GetAudioService();

            if(!audio.IsPlaybackIdle()){
                ESP_LOGI(TAG, "BOOT long press -> STOP SD audio");
                StopSdAudio();
            }else{
                ESP_LOGI(TAG, "BOOT long press -> PLAY SD audio");
                ListSdAudioFiles();
                PlayOggFromSd(SD_MOUNT_POINT "/audio/test1.ogg");
            }
        });

    }

public:
    Spotpear_ESP32_S3_1_28_BOX() : boot_button_(BOOT_BUTTON_GPIO) {
        // 先初始化触摸的I2C并探测/初始化触摸（若无触摸则跳过）
        InitializeCodecI2c_Touch();
        InitializeCst816DTouchPad();

        // 初始化音频I2C
        InitializeCodecI2c();

        // HMB|TEC onboard microSD
        InitializeSdCard();

        // 显示相关先建立起来
        InitializeSpi();

        InitializeGc9a01Display();
        InitializeButtons();
        InitializeTools();
        if (GetBacklight()) {
            GetBacklight()->RestoreBrightness();
        }

        // 显示和背光可用后再初始化省电逻辑，避免空指针
        InitializePowerSaveTimer();
        InitializePowerManager();
    }

    ~Spotpear_ESP32_S3_1_28_BOX() {
        if (touchpad_timer_) {
            esp_timer_stop(touchpad_timer_);
            esp_timer_delete(touchpad_timer_);
            touchpad_timer_ = nullptr;
        }
        if (cst816d_) {
            delete cst816d_;
            cst816d_ = nullptr;
        }
        if (power_save_timer_) {
            delete power_save_timer_;
            power_save_timer_ = nullptr;
        }
        if (power_manager_) {
            delete power_manager_;
            power_manager_ = nullptr;
        }
        if (display_) {
            delete display_;
            display_ = nullptr;
        }
        if (i2c_bus_) {
            i2c_del_master_bus(i2c_bus_);
            i2c_bus_ = nullptr;
        }
        if (codec_i2c_bus_) {
            i2c_del_master_bus(codec_i2c_bus_);
            codec_i2c_bus_ = nullptr;
        }
    }


    virtual Led* GetLed() override {
        static SingleLed led(BUILTIN_LED_GPIO);
        return &led;
    }

    virtual Display* GetDisplay() override {
        return display_;
    }

    virtual Backlight* GetBacklight() override {
        static PwmBacklight backlight(DISPLAY_BACKLIGHT_PIN, DISPLAY_BACKLIGHT_OUTPUT_INVERT);
        return &backlight;
    }

    virtual AudioCodec* GetAudioCodec() override {
        static Es8311AudioCodec audio_codec(codec_i2c_bus_, I2C_NUM_0, AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_GPIO_MCLK, AUDIO_I2S_GPIO_BCLK, AUDIO_I2S_GPIO_WS, AUDIO_I2S_GPIO_DOUT, AUDIO_I2S_GPIO_DIN,
            AUDIO_CODEC_PA_PIN, AUDIO_CODEC_ES8311_ADDR);
        return &audio_codec;
    }

    Cst816d* GetTouchpad() {
        return cst816d_;
    }

    virtual bool GetBatteryLevel(int& level, bool& charging, bool& discharging) override {
        if (!power_manager_) {
            level = 0;
            charging = false;
            discharging = true;
            return false;
        }
        
        static bool last_discharging = false;
        charging = power_manager_->IsCharging();
        discharging = power_manager_->IsDischarging();
        if (discharging != last_discharging) {
            power_save_timer_->SetEnabled(discharging);
            last_discharging = discharging;
        }
        level = power_manager_->GetBatteryLevel();
        return true;
    }

    virtual void SetPowerSaveLevel(PowerSaveLevel level) override {
        if (level != PowerSaveLevel::LOW_POWER) {
            power_save_timer_->WakeUp();
        }
        WifiBoard::SetPowerSaveLevel(level);
    }
};

DECLARE_BOARD(Spotpear_ESP32_S3_1_28_BOX);
