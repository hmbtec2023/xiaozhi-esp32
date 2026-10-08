#pragma once
#ifdef NFC_EN
#include <driver/spi_master.h>
#include <driver/gpio.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <cstring>
#include <string>
// MFRC522 SPI / ISO14443A / MIFARE Classic sector-1 data blocks.
class HmbTalkCardNfc{
    spi_device_handle_t dev_=nullptr;
    uint8_t uid_[10]={}; uint8_t uid_len_=0; uint8_t sak_=0; bool ntag_=false;
    uint8_t rd(uint8_t r){uint8_t tx[2]={uint8_t((r<<1)|0x80),0},rx[2]={};spi_transaction_t t={};t.length=16;t.tx_buffer=tx;t.rx_buffer=rx;if(spi_device_transmit(dev_,&t)!=ESP_OK)return 0;return rx[1];}
    void wr(uint8_t r,uint8_t v){uint8_t tx[2]={uint8_t(r<<1),v};spi_transaction_t t={};t.length=16;t.tx_buffer=tx;spi_device_transmit(dev_,&t);}
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
        spi_bus_config_t bus={};bus.mosi_io_num=NFC_MOSI_GPIO;bus.miso_io_num=NFC_MISO_GPIO;bus.sclk_io_num=NFC_SCK_GPIO;bus.quadwp_io_num=-1;bus.quadhd_io_num=-1;bus.max_transfer_sz=64;
        esp_err_t e=spi_bus_initialize(NFC_SPI_HOST,&bus,SPI_DMA_DISABLED);
        if(e!=ESP_OK){ESP_LOGE("TalkCardNFC","SPI bus: %s",esp_err_to_name(e));return false;}
        spi_device_interface_config_t cfg={};cfg.clock_speed_hz=1000000;cfg.mode=0;cfg.spics_io_num=NFC_SS_GPIO;cfg.queue_size=1;
        e=spi_bus_add_device(NFC_SPI_HOST,&cfg,&dev_);
        if(e!=ESP_OK){ESP_LOGE("TalkCardNFC","SPI device: %s",esp_err_to_name(e));return false;}
        gpio_config_t g={};g.pin_bit_mask=1ULL<<NFC_RST_GPIO;g.mode=GPIO_MODE_OUTPUT;gpio_config(&g);
        gpio_set_level(NFC_RST_GPIO,1);vTaskDelay(pdMS_TO_TICKS(30));
        wr(0x01,0x0f);wr(0x2A,0x8d);wr(0x2B,0x3e);wr(0x2D,30);wr(0x2C,0);wr(0x15,0x40);wr(0x11,0x3d);set(0x14,0x03);
        ESP_LOGI("TalkCardNFC","RC522 version=0x%02X",rd(0x37));
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
        if(!out || capacity<64)return false;
        memset(out,0,capacity);
        if(ntag_){
            for(uint8_t page=4;page<=16;page+=4){
                uint8_t tx[4]={0x30,page,0,0};
                uint16_t c=crc_a(tx,2);tx[2]=uint8_t(c);tx[3]=uint8_t(c>>8);
                uint8_t rx[20]={};size_t n=sizeof(rx);
                if(!transceive(tx,4,rx,n) || n!=18){
                    ESP_LOGW("TalkCardNFC","Type2 READ page=%u failed len=%u",page,(unsigned)n);
                    return false;
                }
                memcpy(out+out_len,rx,16);out_len+=16;
            }
            return true;
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
