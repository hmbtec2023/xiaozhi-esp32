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
    lv_obj_t* eye_root_=nullptr;
    lv_obj_t* eye_white_=nullptr;
    lv_obj_t* iris_outer_=nullptr;
    lv_obj_t* iris_mid_=nullptr;
    lv_obj_t* iris_inner_=nullptr;
    lv_obj_t* pupil_=nullptr;
    lv_obj_t* highlight_big_=nullptr;
    lv_obj_t* highlight_small_=nullptr;
    lv_obj_t* lid_top_=nullptr;
    lv_obj_t* lid_bottom_=nullptr;
    lv_timer_t* eye_timer_=nullptr;
    float eye_x_=0.0f;
    float eye_y_=0.0f;
    float target_x_=0.0f;
    float target_y_=0.0f;
    float blink_=0.0f;
    bool blink_closing_=false;
    bool double_blink_pending_=false;
    int64_t next_look_ms_=0;
    int64_t next_blink_ms_=0;

    static constexpr int EYE_CX=120;
    static constexpr int EYE_CY=130;
    static constexpr int EYE_DIAMETER=218;
    static constexpr int IRIS_OUTER=112;
    static constexpr int IRIS_MID=92;
    static constexpr int IRIS_INNER=70;
    static constexpr int PUPIL=52;
    static constexpr int LOOK_X=30;
    static constexpr int LOOK_Y=21;
    static constexpr int LID_MAX=105;

    static void SetCircle(lv_obj_t* obj,int size,lv_color_t color){
        if(!obj) return;
        lv_obj_set_size(obj,size,size);
        lv_obj_set_style_radius(obj,LV_RADIUS_CIRCLE,0);
        lv_obj_set_style_bg_color(obj,color,0);
        lv_obj_set_style_bg_opa(obj,LV_OPA_COVER,0);
        lv_obj_set_style_border_width(obj,0,0);
        lv_obj_set_style_pad_all(obj,0,0);
        lv_obj_clear_flag(obj,LV_OBJ_FLAG_SCROLLABLE);
    }

    void PositionEye(){
        if(!iris_outer_ || !iris_mid_ || !iris_inner_ || !pupil_ ||
           !highlight_big_ || !highlight_small_) return;

        int x=EYE_CX+(int)eye_x_;
        int y=EYE_CY+(int)eye_y_;

        lv_obj_set_pos(iris_outer_,x-IRIS_OUTER/2,y-IRIS_OUTER/2);
        lv_obj_set_pos(iris_mid_,x-IRIS_MID/2,y-IRIS_MID/2);
        lv_obj_set_pos(iris_inner_,x-IRIS_INNER/2,y-IRIS_INNER/2);
        lv_obj_set_pos(pupil_,x-PUPIL/2,y-PUPIL/2);
        lv_obj_set_pos(highlight_big_,x-23,y-25);
        lv_obj_set_pos(highlight_small_,x+14,y+11);
    }

    void UpdateLids(){
        if(!lid_top_ || !lid_bottom_) return;

        int lid=(int)(LID_MAX*blink_);
        lv_obj_set_height(lid_top_,24+lid);
        lv_obj_set_height(lid_bottom_,20+lid);
    }

    void Animate(){
        if(!eye_root_) return;

        int64_t now=esp_timer_get_time()/1000;
        DeviceState state=Application::GetInstance().GetDeviceState();

        if(now>=next_look_ms_){
            if(state==kDeviceStateListening){
                target_x_=0.0f;
                target_y_=0.0f;
                next_look_ms_=now+900;
            }else if(state==kDeviceStateSpeaking){
                target_x_=(float)((int)(esp_random()%41)-20);
                target_y_=(float)((int)(esp_random()%25)-12);
                next_look_ms_=now+650+(esp_random()%550);
            }else{
                target_x_=(float)((int)(esp_random()%(LOOK_X*2+1))-LOOK_X);
                target_y_=(float)((int)(esp_random()%(LOOK_Y*2+1))-LOOK_Y);
                next_look_ms_=now+900+(esp_random()%2200);
            }
        }

        eye_x_+=(target_x_-eye_x_)*0.16f;
        eye_y_+=(target_y_-eye_y_)*0.16f;
        PositionEye();

        if(now>=next_blink_ms_ && !blink_closing_ && blink_<=0.0f){
            blink_closing_=true;
            double_blink_pending_=(esp_random()%8)==0;
        }

        if(blink_closing_){
            blink_+=0.34f;

            if(blink_>=1.0f){
                blink_=1.0f;
                blink_closing_=false;
            }
        }else if(blink_>0.0f){
            blink_-=0.26f;

            if(blink_<=0.0f){
                blink_=0.0f;

                if(double_blink_pending_){
                    double_blink_pending_=false;
                    next_blink_ms_=now+170;
                }else{
                    next_blink_ms_=now+2800+(esp_random()%4200);
                }
            }
        }

        UpdateLids();
    }

    static void EyeTimerCallback(lv_timer_t* timer){
        auto* self=static_cast<CustomLcdDisplay*>(lv_timer_get_user_data(timer));
        if(self) self->Animate();
    }

    static void SetupEyeAsync(void* user_data){
        auto* self=static_cast<CustomLcdDisplay*>(user_data);
        if(self) self->SetupEye();
    }

    void SetupEye(){
        if(eye_root_){
            ESP_LOGW(TAG,"SetupEye ignored: eye already initialized");
            return;
        }

        ESP_LOGI(TAG,"SetupEye start");

        lv_obj_t* screen=lv_screen_active();
        if(!screen){
            ESP_LOGE(TAG,"SetupEye failed: no active LVGL screen");
            return;
        }

        // ---------------------------------------------------------------------
        // HMB | TEC SingleEye root layer
        // ---------------------------------------------------------------------
        // Die originale XiaoZhi-Oberflaeche wird nicht veraendert.
        // Das Auge liegt als eigene schwarze Ebene darueber.
        eye_root_=lv_obj_create(screen);
        if(!eye_root_){
            ESP_LOGE(TAG,"SetupEye failed: eye_root creation failed");
            return;
        }

        lv_obj_set_size(eye_root_,240,240);
        lv_obj_set_pos(eye_root_,0,0);
        lv_obj_set_style_bg_color(eye_root_,lv_color_black(),0);
        lv_obj_set_style_bg_opa(eye_root_,LV_OPA_COVER,0);
        lv_obj_set_style_border_width(eye_root_,0,0);
        lv_obj_set_style_pad_all(eye_root_,0,0);
        lv_obj_set_style_radius(eye_root_,0,0);
        lv_obj_clear_flag(eye_root_,LV_OBJ_FLAG_SCROLLABLE);

        // ---------------------------------------------------------------------
        // Eye white
        // ---------------------------------------------------------------------
        eye_white_=lv_obj_create(eye_root_);
        SetCircle(eye_white_,EYE_DIAMETER,lv_color_white());
        lv_obj_set_pos(
            eye_white_,
            EYE_CX-EYE_DIAMETER/2,
            EYE_CY-EYE_DIAMETER/2
        );

        // ---------------------------------------------------------------------
        // Iris colors
        // ---------------------------------------------------------------------
#if HMB_EYE_IRIS_COLOR == HMB_EYE_IRIS_BROWN
        const lv_color_t iris1=lv_color_hex(0x9A6538);
        const lv_color_t iris2=lv_color_hex(0x70431F);
        const lv_color_t iris3=lv_color_hex(0xB6814E);
#else
        const lv_color_t iris1=lv_color_hex(0x2388C7);
        const lv_color_t iris2=lv_color_hex(0x12649B);
        const lv_color_t iris3=lv_color_hex(0x55B8E8);
#endif

        // ---------------------------------------------------------------------
        // Iris / pupil / highlights
        // ---------------------------------------------------------------------
        iris_outer_=lv_obj_create(eye_root_);
        iris_mid_=lv_obj_create(eye_root_);
        iris_inner_=lv_obj_create(eye_root_);
        pupil_=lv_obj_create(eye_root_);
        highlight_big_=lv_obj_create(eye_root_);
        highlight_small_=lv_obj_create(eye_root_);

        SetCircle(iris_outer_,IRIS_OUTER,iris1);
        SetCircle(iris_mid_,IRIS_MID,iris2);
        SetCircle(iris_inner_,IRIS_INNER,iris3);
        SetCircle(pupil_,PUPIL,lv_color_black());
        SetCircle(highlight_big_,18,lv_color_white());
        SetCircle(highlight_small_,8,lv_color_white());

        PositionEye();

        // ---------------------------------------------------------------------
        // Eyelids
        // ---------------------------------------------------------------------
        lid_top_=lv_obj_create(eye_root_);
        lid_bottom_=lv_obj_create(eye_root_);

        for(auto* lid:{lid_top_,lid_bottom_}){
            lv_obj_set_width(lid,240);
            lv_obj_set_style_bg_color(lid,lv_color_black(),0);
            lv_obj_set_style_bg_opa(lid,LV_OPA_COVER,0);
            lv_obj_set_style_border_width(lid,0,0);
            lv_obj_set_style_pad_all(lid,0,0);
            lv_obj_set_style_radius(lid,70,0);
            lv_obj_clear_flag(lid,LV_OBJ_FLAG_SCROLLABLE);
        }

        lv_obj_align(lid_top_,LV_ALIGN_TOP_MID,0,-34);
        lv_obj_align(lid_bottom_,LV_ALIGN_BOTTOM_MID,0,34);

        UpdateLids();

        // ---------------------------------------------------------------------
        // XiaoZhi status bar remains visible above eye
        // ---------------------------------------------------------------------
        if(status_bar_){
            lv_obj_move_foreground(status_bar_);
        }

        if(status_label_){
            lv_obj_set_style_text_color(status_label_,lv_color_white(),0);
        }

        // ---------------------------------------------------------------------
        // Animation
        // ---------------------------------------------------------------------
        int64_t now=esp_timer_get_time()/1000;
        next_look_ms_=now+700;
        next_blink_ms_=now+2200;

        if(!eye_timer_){
            eye_timer_=lv_timer_create(EyeTimerCallback,45,this);
        }

        ESP_LOGI(TAG,"SetupEye complete");
        ESP_LOGI(
            TAG,
            "HMB|TEC SingleEye active, iris=%s",
            HMB_EYE_IRIS_COLOR==HMB_EYE_IRIS_BROWN ? "brown" : "blue"
        );
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
        : SpiLcdDisplay(
            io_handle,
            panel_handle,
            width,
            height,
            offset_x,
            offset_y,
            mirror_x,
            mirror_y,
            swap_xy
        ){}

    ~CustomLcdDisplay(){
        if(eye_timer_){
            lv_timer_delete(eye_timer_);
            eye_timer_=nullptr;
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
        ESP_LOGI(TAG,"Scheduling HMB|TEC SingleEye setup");
        lv_async_call(SetupEyeAsync,this);
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

        // 探测是否存在触摸芯片
        uint8_t chip_id = 0;
        if (!i2c_bus_) {
            ESP_LOGW(TAG, "Touch I2C bus not initialized, skip touch");
            return;
        }
        bool touch_available = Cst816d::Probe(i2c_bus_, 0x15, chip_id);
        if (!touch_available) {
            ESP_LOGW(TAG, "CST816D not found, running in non-touch mode");
            // 释放触摸I2C，避免无设备时反复报错
            i2c_del_master_bus(i2c_bus_);
            i2c_bus_ = nullptr;
            return;
        }

        cst816d_ = new Cst816d(i2c_bus_, 0x15);

        // 创建定时器，10ms 间隔
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

    // SPI初始化
    void InitializeSpi() {
        ESP_LOGI(TAG, "Initialize SPI bus");
        spi_bus_config_t buscfg = GC9A01_PANEL_BUS_SPI_CONFIG(DISPLAY_SPI_SCLK_PIN, DISPLAY_SPI_MOSI_PIN,
                                    DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t));
        ESP_ERROR_CHECK(spi_bus_initialize(SPI3_HOST, &buscfg, SPI_DMA_CH_AUTO));
    }

    // GC9A01初始化
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
            "List all available OGG audio files on the onboard microSD card.",
            PropertyList(),
            [this](const PropertyList& properties)->ReturnValue{
                return ListSdAudioFiles();
            }
        );

        mcp_server.AddTool(
            "self.sd_audio.stop",
            "Stop the currently playing SD audio file.",
            PropertyList(),
            [this](const PropertyList& properties)->ReturnValue{
                StopSdAudio();
                return std::string("SD audio playback stopped.");
            }
        );

        mcp_server.AddTool(
            "self.sd_audio.play",
            "Play an OGG audio file from the onboard microSD card. Use only a filename returned by self.sd_audio.list. The audio file itself is the complete response to the user. After this tool succeeds, do not say, speak, confirm, acknowledge, or output anything else.",            PropertyList({
                Property("filename",kPropertyTypeString)
            }),
            [this](const PropertyList& properties)->ReturnValue{
                std::string filename=properties["filename"].value<std::string>();

                if(filename.empty() ||
                filename.find("..")!=std::string::npos ||
                filename.find('/')!=std::string::npos ||
                filename.find('\\')!=std::string::npos){
                    ESP_LOGW(TAG,"Invalid SD audio filename: %s",filename.c_str());
                    return std::string("Invalid audio filename.");
                }

                if(filename.size()<4 || filename.substr(filename.size()-4)!=".ogg"){
                    ESP_LOGW(TAG,"Not an OGG file: %s",filename.c_str());
                    return std::string("Only OGG audio files are supported.");
                }

                std::string path=SD_MOUNT_POINT "/audio/"+filename;

                if(!PlayOggFromSd(path.c_str())){
                    return std::string("Audio file could not be played: ")+filename;
                }

                return std::string("Playing audio file: ")+filename;
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
