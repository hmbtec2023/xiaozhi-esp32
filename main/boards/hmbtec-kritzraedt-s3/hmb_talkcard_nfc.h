#pragma once
#ifdef NFC_EN
#include <driver/spi_master.h>
#include <esp_rom_sys.h>
#include <driver/gpio.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <cstring>
#include <string>
// MFRC522 SPI / ISO14443A / MIFARE Classic sector-1 data blocks.
class HmbTalkCardNfc{
    spi_device_handle_t dev_=nullptr;
    // Software-SPI Mode 0, MSB first; bewusst langsam fuer robuste Verdrahtung.
    uint8_t transfer_byte(uint8_t value) {
        uint8_t received = 0;
        for (int bit = 7; bit >= 0; --bit) {
            gpio_set_level(NFC_SCK_GPIO, 0);
            gpio_set_level(NFC_MOSI_GPIO, (value >> bit) & 1);
            esp_rom_delay_us(2);
            gpio_set_level(NFC_SCK_GPIO, 1);
            esp_rom_delay_us(2);
            received = (received << 1) | gpio_get_level(NFC_MISO_GPIO);
        }
        gpio_set_level(NFC_SCK_GPIO, 0);
        return received;
    }
    void select() { gpio_set_level(NFC_SS_GPIO, 0); esp_rom_delay_us(2); }
    void deselect() { esp_rom_delay_us(2); gpio_set_level(NFC_SS_GPIO, 1); }

    uint8_t uid_[10]={}; uint8_t uid_len_=0; uint8_t sak_=0; bool ntag_=false;
    uint8_t rd(uint8_t r) {
        select(); transfer_byte(uint8_t((r << 1) | 0x80));
        uint8_t value = transfer_byte(0); deselect(); return value;
    }
    void wr(uint8_t r, uint8_t v) {
        select(); transfer_byte(uint8_t(r << 1)); transfer_byte(v); deselect();
    }
    void set(uint8_t r,uint8_t m){wr(r,rd(r)|m);} void clr(uint8_t r,uint8_t m){wr(r,rd(r)&~m);}
    static uint16_t crc_a(const uint8_t* p,size_t n){uint16_t c=0x6363;while(n--){uint8_t x=*p++ ^ (c&255);x^=x<<4;c=(c>>8) ^ (uint16_t(x)<<8) ^ (uint16_t(x)<<3) ^ (uint16_t(x)>>4);}return c;}
    bool transceive(const uint8_t* tx,size_t n,uint8_t* rx,size_t& rn,uint8_t bits=0){
        wr(0x01,0x00);                  // Idle
        wr(0x04,0x7F);                  // Clear pending IRQ flags
        wr(0x0A,0x80);                  // Flush FIFO
        for(size_t i=0;i<n;i++)wr(0x09,tx[i]);
        wr(0x0D,bits&0x07);             // TxLastBits
        wr(0x01,0x0C);                  // Transceive
        set(0x0D,0x80);                 // StartSend
        bool completed=false;
        for(int wait=0;wait<3000;wait++){
            uint8_t irq=rd(0x04);
            if(irq&0x30){completed=true;break;}
            if(irq&0x01)break;
        }
        clr(0x0D,0x80);
        if(!completed)return false;
        uint8_t error=rd(0x06);
        if(error&0x13)return false;
        size_t len=rd(0x0A);
        if(len==0 || len>rn)return false;
        for(size_t i=0;i<len;i++)rx[i]=rd(0x09);
        rn=len;
        return true;
    }
    bool auth(uint8_t block){
        uint8_t tx[12]={0x60,block,0xff,0xff,0xff,0xff,0xff,0xff};
        if(uid_len_!=4)return false;
        memcpy(tx+8,uid_,4);
        wr(0x01,0x00);
        wr(0x04,0x7F);
        wr(0x0A,0x80);
        for(auto b:tx)wr(0x09,b);
        wr(0x01,0x0E);
        int wait=3000;
        while(wait--){if(rd(0x04)&0x10)break;}
        return wait>0 && (rd(0x08)&0x08);
    }
    void stop(){clr(0x08,0x08);}
    bool command_ack(uint8_t cmd,uint8_t block){uint8_t tx[4]={cmd,block,0,0};uint16_t c=crc_a(tx,2);tx[2]=c&255;tx[3]=c>>8;uint8_t rx[8];size_t n=sizeof(rx);return transceive(tx,4,rx,n) && n==1 && (rx[0]&0x0f)==0x0a;}
public:
    bool begin(){
        gpio_config_t output = {};
        output.pin_bit_mask = (1ULL << NFC_SCK_GPIO) | (1ULL << NFC_MOSI_GPIO) | (1ULL << NFC_SS_GPIO);
        output.mode = GPIO_MODE_OUTPUT;
        output.pull_up_en = GPIO_PULLUP_DISABLE;
        output.pull_down_en = GPIO_PULLDOWN_DISABLE;
        ESP_ERROR_CHECK(gpio_config(&output));
        gpio_config_t input = {};
        input.pin_bit_mask = (1ULL << NFC_MISO_GPIO);
        input.mode = GPIO_MODE_INPUT;
        ESP_ERROR_CHECK(gpio_config(&input));
        gpio_set_level(NFC_SS_GPIO, 1);
        gpio_set_level(NFC_SCK_GPIO, 0);
        gpio_set_level(NFC_MOSI_GPIO, 0);
        // RC522 Hardware-Reset ueber GPIO40
        gpio_set_level(NFC_RST_GPIO, 1);
        gpio_set_direction(NFC_RST_GPIO, GPIO_MODE_OUTPUT);
        esp_rom_delay_us(1000);
        gpio_set_level(NFC_RST_GPIO, 0);
        vTaskDelay(pdMS_TO_TICKS(10));
        gpio_set_level(NFC_RST_GPIO, 1);
        vTaskDelay(pdMS_TO_TICKS(50));

        wr(0x01,0x0f); wr(0x2A,0x8d); wr(0x2B,0x3e);
        wr(0x2D,30); wr(0x2C,0); wr(0x15,0x40);
        wr(0x11,0x3d); set(0x14,0x03);
        uint8_t version = rd(0x37);
        ESP_LOGI("TalkCardNFC", "RC522 Software-SPI version=0x%02X", version);
        if (version == 0x00 || version == 0xFF) {
            ESP_LOGE("TalkCardNFC", "RC522 nicht erreichbar; GPIO/Versorgung/NRSTPD pruefen");
            return false;
        }
        return true;
    }
    bool detect(std::string& uid){
        uint8_t req=0x26,rx[20]={};size_t n=sizeof(rx);
        if(!transceive(&req,1,rx,n,7))return false;
        if(n!=2){ESP_LOGW("TalkCardNFC","Invalid ATQA len=%u",(unsigned)n);return false;}
        ESP_LOGI("TalkCardNFC","ATQA=%02X %02X",rx[0],rx[1]);
        uid_len_=0;sak_=0;ntag_=false;uid.clear();
        const uint8_t cascade[3]={0x93,0x95,0x97};
        for(int level=0;level<3;level++){
            uint8_t anticoll[2]={cascade[level],0x20};n=sizeof(rx);
            if(!transceive(anticoll,2,rx,n) || n!=5){ESP_LOGW("TalkCardNFC","CL%d anticollision failed len=%u",level+1,(unsigned)n);return false;}
            uint8_t bcc=rx[0]^rx[1]^rx[2]^rx[3];
            if(bcc!=rx[4]){ESP_LOGW("TalkCardNFC","CL%d BCC mismatch",level+1);return false;}
            bool ct=(rx[0]==0x88);
            if(ct && level==2){ESP_LOGW("TalkCardNFC","Unexpected cascade at CL3");return false;}
            size_t count=ct?3:4;
            if(uid_len_+count>sizeof(uid_))return false;
            memcpy(uid_+uid_len_,rx+(ct?1:0),count);uid_len_+=count;
            uint8_t sel[9]={cascade[level],0x70,rx[0],rx[1],rx[2],rx[3],rx[4],0,0};
            uint16_t c=crc_a(sel,7);sel[7]=uint8_t(c);sel[8]=uint8_t(c>>8);
            n=sizeof(rx);
            if(!transceive(sel,9,rx,n) || n!=3){ESP_LOGW("TalkCardNFC","CL%d SELECT failed len=%u",level+1,(unsigned)n);return false;}
            sak_=rx[0];bool more=(sak_&0x04)!=0;
            if(more!=ct){ESP_LOGW("TalkCardNFC","CL%d cascade mismatch SAK=0x%02X",level+1,sak_);return false;}
            if(!more)break;
        }
        if(uid_len_!=4 && uid_len_!=7 && uid_len_!=10)return false;
        static const char* hex="0123456789ABCDEF";
        for(uint8_t i=0;i<uid_len_;i++){uid+=hex[uid_[i]>>4];uid+=hex[uid_[i]&15];}
        // SAK 0x00: ISO14443A Type 2 candidate (e.g. NTAG21x/Ultralight).
        // SAK 0x08/0x18: MIFARE Classic candidates; exact product cannot be proven from SAK alone.
        ntag_=(sak_==0x00);
        ESP_LOGI("TalkCardNFC","UID=%s bytes=%u SAK=0x%02X type=%s",uid.c_str(),uid_len_,sak_,ntag_?"Type2 candidate":((sak_==0x08||sak_==0x18)?"Classic candidate":"other"));
        return true;
    }
    bool is_type2() const{return ntag_;}
    uint8_t sak() const{return sak_;}
    // Type 2 READ returns four 4-byte pages (16 data bytes + 2 CRC bytes).
    // Classic READ returns one 16-byte block after sector authentication.
    // Classic sector 1 contains only three data blocks (4,5,6); block 7 is a trailer.
    bool read_payload(uint8_t* out,size_t capacity,size_t& out_len){
        out_len=0;
        if(!out || capacity<48)return false;
        memset(out,0,capacity);
        if(ntag_){
            // Read four pages per transaction. Stop at complete NDEF TLV, not a fixed 64-byte limit.
            size_t required=0;
            for(unsigned page=4;out_len+16<=capacity && page<=252;page+=4){
                uint8_t tx[4]={0x30,uint8_t(page),0,0};
                uint16_t c=crc_a(tx,2);tx[2]=uint8_t(c);tx[3]=uint8_t(c>>8);
                uint8_t rx[20]={};size_t n=sizeof(rx);
                if(!transceive(tx,4,rx,n) || n!=18){
                    ESP_LOGW("TalkCardNFC","Type2 READ page=%u failed len=%u",page,(unsigned)n);
                    return false;
                }
                memcpy(out+out_len,rx,16);out_len+=16;
                // TLV can be preceded by NULL TLVs; supports short and extended lengths.
                size_t pos=0;
                while(pos<out_len){
                    uint8_t type=out[pos++];
                    if(type==0x00)continue;
                    if(type==0xFE){required=pos;break;}
                    if(pos>=out_len)break;
                    size_t length=out[pos++];
                    if(length==0xFF){
                        if(pos+2>out_len)break;
                        length=(size_t(out[pos])<<8)|out[pos+1];pos+=2;
                    }
                    if(length>capacity || pos+length>capacity){
                        ESP_LOGW("TalkCardNFC","Type2 TLV exceeds buffer (%u)",(unsigned)length);
                        return false;
                    }
                    if(type==0x03){required=pos+length;break;}
                    if(pos+length>out_len)break;
                    pos+=length;
                }
                if(required && out_len>=required)return true;
            }
            ESP_LOGW("TalkCardNFC","Type2 TLV incomplete or exceeds %u bytes",(unsigned)capacity);
            return false;
        }
        if(sak_!=0x08 && sak_!=0x18)return false;
        for(uint8_t block=4;block<=6;block++){
            if(!auth(block)){stop();ESP_LOGW("TalkCardNFC","Classic AUTH block=%u failed",block);return false;}
            uint8_t tx[4]={0x30,block,0,0};uint16_t c=crc_a(tx,2);
            tx[2]=uint8_t(c);tx[3]=uint8_t(c>>8);
            uint8_t rx[20]={};size_t n=sizeof(rx);
            bool ok=transceive(tx,4,rx,n) && n==18;
            if(ok){memcpy(out+out_len,rx,16);out_len+=16;}
            stop();
            if(!ok){ESP_LOGW("TalkCardNFC","Classic READ block=%u failed",block);return false;}
        }
        return true;
    }
    bool read(uint8_t block,uint8_t data[16]){
        if(ntag_){
            if(block<4 || block>6)return false;
            uint8_t tx[4]={0x30,block,0,0};uint16_t c=crc_a(tx,2);tx[2]=uint8_t(c);tx[3]=uint8_t(c>>8);
            uint8_t rx[20];size_t n=sizeof(rx);
            bool ok=transceive(tx,4,rx,n) && n==18;
            if(ok)memcpy(data,rx,16);
            return ok;
        }
        if(block<4 || block>6 || !auth(block)){stop();return false;}uint8_t tx[4]={0x30,block,0,0};uint16_t c=crc_a(tx,2);tx[2]=c&255;tx[3]=c>>8;uint8_t rx[20];size_t n=sizeof(rx);bool ok=transceive(tx,4,rx,n)&&n==18;if(ok)memcpy(data,rx,16);stop();return ok;}
    bool write(uint8_t block,const uint8_t data[16]){if(ntag_)return false;if(block<4 || block>6 || !auth(block)){stop();return false;}bool ok=command_ack(0xA0,block);if(ok){uint8_t tx[18];memcpy(tx,data,16);uint16_t c=crc_a(tx,16);tx[16]=c&255;tx[17]=c>>8;uint8_t rx[4];size_t n=sizeof(rx);ok=transceive(tx,18,rx,n)&&n==1&&(rx[0]&15)==0x0a;}stop();return ok;}
};
#endif
