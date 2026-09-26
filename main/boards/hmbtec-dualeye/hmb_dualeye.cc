#include "hmb_dualeye.h"
#include "config.h"

#if HMB_DUALEYE_ENABLED

#include <driver/gpio.h>
#include <driver/spi_master.h>
#include <esp_log.h>
#include <esp_random.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <algorithm>
#include <cmath>
#include <cstring>

// ============================================================================
// HMB | TEC - XiaoZhi DualEye Engine
// ============================================================================

static const char* TAG = "HmbDualEye";

// -----------------------------------------------------------------------------
// Display
// -----------------------------------------------------------------------------

static constexpr int W = 160;
static constexpr int H = 160;
static constexpr int N = W * H;

// -----------------------------------------------------------------------------
// Farben RGB565
// -----------------------------------------------------------------------------

static constexpr uint16_t BLACK = 0x0000;
static constexpr uint16_t WHITE = 0xFFFF;

static constexpr uint16_t IRIS1 = 0x067F;
static constexpr uint16_t IRIS2 = 0x04FC;
static constexpr uint16_t IRIS3 = 0x0255;

// -----------------------------------------------------------------------------
// Framebuffer / SPI
// -----------------------------------------------------------------------------

static uint16_t fb[N];

static spi_device_handle_t devL = nullptr;
static spi_device_handle_t devR = nullptr;

// -----------------------------------------------------------------------------
// Eye State
// -----------------------------------------------------------------------------

static volatile DeviceState eyeState = kDeviceStateIdle;

static float px = 0.0f;
static float py = 0.0f;

static float tx = 0.0f;
static float ty = 0.0f;

static float blink = 0.0f;

static bool closing = false;

static int64_t nextLook = 0;
static int64_t nextBlink = 0;

// ============================================================================
// SPI Low Level
// ============================================================================

static void txBytes(spi_device_handle_t d, const void* data, size_t len){
    spi_transaction_t t = {};
    t.length = len * 8;
    t.tx_buffer = data;

    ESP_ERROR_CHECK(spi_device_polling_transmit(d, &t));
}

static void cmd(spi_device_handle_t d, uint8_t c){
    gpio_set_level(HMB_EYE_DC, 0);
    txBytes(d, &c, 1);
}

static void data(spi_device_handle_t d, const void* p, size_t n){
    gpio_set_level(HMB_EYE_DC, 1);
    txBytes(d, p, n);
}

static void cd(spi_device_handle_t d, uint8_t c, const uint8_t* p, size_t n){
    cmd(d, c);

    if(n){
        data(d, p, n);
    }
}

// ============================================================================
// GC9D01 Initialisierung
// ============================================================================

static void initPanel(spi_device_handle_t d){
    cmd(d, 0xFE);
    cmd(d, 0xEF);

    for(uint8_t r = 0x80; r <= 0x8F; r++){
        uint8_t v = 0xFF;
        cd(d, r, &v, 1);
    }

    uint8_t v;

    v = 0x05;
    cd(d, 0x3A, &v, 1);

    v = 0x01;
    cd(d, 0xEC, &v, 1);

    const uint8_t a74[] = {
        0x02, 0x0E, 0x00, 0x00, 0x00, 0x00, 0x00
    };
    cd(d, 0x74, a74, sizeof(a74));

    v = 0x3E;
    cd(d, 0x98, &v, 1);
    cd(d, 0x99, &v, 1);

    const uint8_t b5[] = {
        0x0D, 0x0D
    };
    cd(d, 0xB5, b5, sizeof(b5));

    const uint8_t a60[] = {
        0x38, 0x0F, 0x79, 0x67
    };
    cd(d, 0x60, a60, sizeof(a60));

    const uint8_t a61[] = {
        0x38, 0x11, 0x79, 0x67
    };
    cd(d, 0x61, a61, sizeof(a61));

    const uint8_t a64[] = {
        0x38, 0x17, 0x71, 0x5F, 0x79, 0x67
    };
    cd(d, 0x64, a64, sizeof(a64));

    const uint8_t a65[] = {
        0x38, 0x13, 0x71, 0x5B, 0x79, 0x67
    };
    cd(d, 0x65, a65, sizeof(a65));

    const uint8_t a6a[] = {
        0x00, 0x00
    };
    cd(d, 0x6A, a6a, sizeof(a6a));

    const uint8_t a6c[] = {
        0x22, 0x02, 0x22, 0x02, 0x22, 0x22, 0x50
    };
    cd(d, 0x6C, a6c, sizeof(a6c));

    const uint8_t a6e[] = {
        0x03, 0x03, 0x01, 0x01,
        0x00, 0x00, 0x0F, 0x0F,
        0x0D, 0x0D, 0x0B, 0x0B,
        0x09, 0x09, 0x00, 0x00,
        0x00, 0x00, 0x0A, 0x0A,
        0x0C, 0x0C, 0x0E, 0x0E,
        0x10, 0x10, 0x00, 0x00,
        0x02, 0x02, 0x04, 0x04
    };
    cd(d, 0x6E, a6e, sizeof(a6e));

    v = 0x01;
    cd(d, 0xBF, &v, 1);

    v = 0x40;
    cd(d, 0xF9, &v, 1);

    v = 0x3B;
    cd(d, 0x9B, &v, 1);

    const uint8_t a93[] = {
        0x33, 0x7F, 0x00
    };
    cd(d, 0x93, a93, sizeof(a93));

    v = 0x30;
    cd(d, 0x7E, &v, 1);

    const uint8_t a70[] = {
        0x0D, 0x02, 0x08,
        0x0D, 0x02, 0x08
    };
    cd(d, 0x70, a70, sizeof(a70));

    const uint8_t a71[] = {
        0x0D, 0x02, 0x08
    };
    cd(d, 0x71, a71, sizeof(a71));

    const uint8_t a91[] = {
        0x0E, 0x09
    };
    cd(d, 0x91, a91, sizeof(a91));

    v = 0x19;
    cd(d, 0xC3, &v, 1);
    cd(d, 0xC4, &v, 1);

    v = 0x3C;
    cd(d, 0xC9, &v, 1);

    const uint8_t f0[] = {
        0x53, 0x15, 0x0A, 0x04, 0x00, 0x3E
    };
    cd(d, 0xF0, f0, sizeof(f0));

    const uint8_t f2[] = {
        0x53, 0x15, 0x0A, 0x04, 0x00, 0x3A
    };
    cd(d, 0xF2, f2, sizeof(f2));

    const uint8_t f1[] = {
        0x56, 0xA8, 0x7F, 0x33, 0x34, 0x5F
    };
    cd(d, 0xF1, f1, sizeof(f1));

    const uint8_t f3[] = {
        0x52, 0xA4, 0x7F, 0x33, 0x34, 0xDF
    };
    cd(d, 0xF3, f3, sizeof(f3));

    // Display orientation
    v = 0x60;
    cd(d, 0x36, &v, 1);

    // Sleep Out
    cmd(d, 0x11);
    vTaskDelay(pdMS_TO_TICKS(200));

    // Display On
    cmd(d, 0x29);
    vTaskDelay(pdMS_TO_TICKS(20));
}

// ============================================================================
// Primitive Grafikfunktionen
// ============================================================================

static void hline(int x, int y, int w, uint16_t c){
    if(y < 0 || y >= H || w <= 0){
        return;
    }

    if(x < 0){
        w += x;
        x = 0;
    }

    if(x + w > W){
        w = W - x;
    }

    if(w <= 0){
        return;
    }

    for(int i = 0; i < w; i++){
        fb[y * W + x + i] = c;
    }
}

static void circle(int cx, int cy, int r, uint16_t c){
    int r2 = r * r;

    for(int y = -r; y <= r; y++){
        int x = (int)sqrtf((float)(r2 - y * y));
        hline(cx - x, cy + y, x * 2 + 1, c);
    }
}

// ============================================================================
// Auge rendern
// ============================================================================

static void render(){
    std::fill_n(fb, N, BLACK);

    // Augapfel
    circle(80, 80, 74, WHITE);

    // Irisposition
    int ix = 80 + (int)px;
    int iy = 80 + (int)py;

    // Iris
    circle(ix, iy, 38, IRIS1);
    circle(ix, iy, 31, IRIS2);
    circle(ix, iy, 24, IRIS3);

    // Pupille
    circle(ix, iy, 18, BLACK);

    // Lichtreflexe
    circle(ix - 10, iy - 11, 7, WHITE);
    circle(ix + 9, iy + 8, 3, WHITE);

    // Augenlider / Blinzeln
    int lid = (int)(82.0f * blink);

    for(int y = 0; y < lid; y++){
        int in = (int)(10.0f * (1.0f - y / 82.0f));

        hline(in, y, W - in * 2, BLACK);
        hline(in, H - 1 - y, W - in * 2, BLACK);
    }
}

// ============================================================================
// Frame an Display übertragen
// ============================================================================

static void push(spi_device_handle_t d){
    const uint8_t col[] = {
        0x00, 0x00, 0x00, 0x9F
    };

    const uint8_t row[] = {
        0x00, 0x00, 0x00, 0x9F
    };

    cd(d, 0x2A, col, sizeof(col));
    cd(d, 0x2B, row, sizeof(row));

    cmd(d, 0x2C);

    gpio_set_level(HMB_EYE_DC, 1);

    static uint8_t line[W * 2];

    for(int y = 0; y < H; y++){
        for(int x = 0; x < W; x++){
            uint16_t c = fb[y * W + x];

            line[x * 2] = c >> 8;
            line[x * 2 + 1] = c & 0xFF;
        }

        txBytes(d, line, sizeof(line));
    }
}

// ============================================================================
// Zufällige Blickposition
//
// WICHTIG:
// esp_random() liefert uint32_t.
// Vor der Subtraktion muss explizit nach int gewandelt werden.
// Sonst kann bei negativen Sollwerten ein unsigned Unterlauf entstehen.
// ============================================================================

static int randomSigned(int minimum, int maximum){
    uint32_t range = (uint32_t)(maximum - minimum + 1);
    return (int)(esp_random() % range) + minimum;
}

// ============================================================================
// Eye Animation Task
// ============================================================================

static void task(void*){
    nextLook = esp_timer_get_time() / 1000 + 800;
    nextBlink = esp_timer_get_time() / 1000 + 3000;

    while(true){
        int64_t now = esp_timer_get_time() / 1000;
        DeviceState s = eyeState;

        // ---------------------------------------------------------------------
        // XiaoZhi State -> Blickverhalten
        // ---------------------------------------------------------------------

        if(s == kDeviceStateListening){
            // Beim Zuhören Benutzer ansehen
            tx = 0.0f;
            ty = 0.0f;
        }
        else if(
            s == kDeviceStateConnecting ||
            s == kDeviceStateWifiConfiguring
        ){
            // Suchende, etwas schnellere Augenbewegung
            if(now >= nextLook){
                tx = (float)randomSigned(-23, 23);
                ty = (float)randomSigned(-10, 10);

                nextLook = now + 500;
            }
        }
        else if(s == kDeviceStateSpeaking){
            // Kleine lebendige Bewegungen während der Sprachausgabe
            if(now >= nextLook){
                tx = (float)randomSigned(-15, 15);
                ty = (float)randomSigned(-10, 10);

                nextLook = now + 700;
            }
        }
        else if(now >= nextLook){
            // Normaler Idle-Blick
            tx = (float)randomSigned(-23, 23);
            ty = (float)randomSigned(-18, 18);

            nextLook =
                now +
                800 +
                (int64_t)(esp_random() % 1800);
        }

        // ---------------------------------------------------------------------
        // Weiche Augenbewegung
        // ---------------------------------------------------------------------

        px += (tx - px) * 0.18f;
        py += (ty - py) * 0.18f;

        // Zusätzliche Sicherheitsbegrenzung
        px = std::max(-23.0f, std::min(23.0f, px));
        py = std::max(-18.0f, std::min(18.0f, py));

        // ---------------------------------------------------------------------
        // Automatisches Blinzeln
        // ---------------------------------------------------------------------

        if(now >= nextBlink && !closing && blink <= 0.0f){
            closing = true;

            nextBlink =
                now +
                2500 +
                (int64_t)(esp_random() % 3500);
        }

        if(closing){
            blink += 0.32f;

            if(blink >= 1.0f){
                blink = 1.0f;
                closing = false;
            }
        }
        else if(blink > 0.0f){
            blink -= 0.24f;

            if(blink < 0.0f){
                blink = 0.0f;
            }
        }

        // ---------------------------------------------------------------------
        // Render + beide Displays aktualisieren
        // ---------------------------------------------------------------------

        render();

        push(devL);
        push(devR);

        vTaskDelay(pdMS_TO_TICKS(45));
    }
}

// ============================================================================
// HmbDualEye
// ============================================================================

void HmbDualEye::Init(){
    ESP_LOGI(TAG, "Initializing HMBTEC DualEye");

    // -------------------------------------------------------------------------
    // GPIO
    // -------------------------------------------------------------------------

    gpio_config_t g = {};

    g.pin_bit_mask =
        (1ULL << HMB_EYE_DC) |
        (1ULL << HMB_EYE_RST_LEFT) |
        (1ULL << HMB_EYE_RST_RIGHT);

    g.mode = GPIO_MODE_OUTPUT;

    ESP_ERROR_CHECK(gpio_config(&g));

    // -------------------------------------------------------------------------
    // Beide Displays resetten
    // -------------------------------------------------------------------------

    gpio_set_level(HMB_EYE_RST_LEFT, 0);
    gpio_set_level(HMB_EYE_RST_RIGHT, 0);

    vTaskDelay(pdMS_TO_TICKS(30));

    gpio_set_level(HMB_EYE_RST_LEFT, 1);
    gpio_set_level(HMB_EYE_RST_RIGHT, 1);

    vTaskDelay(pdMS_TO_TICKS(150));

    // -------------------------------------------------------------------------
    // SPI Bus
    // -------------------------------------------------------------------------

    spi_bus_config_t bus = {};

    bus.mosi_io_num = HMB_EYE_MOSI;
    bus.miso_io_num = -1;
    bus.sclk_io_num = HMB_EYE_SCLK;
    bus.quadwp_io_num = -1;
    bus.quadhd_io_num = -1;
    bus.max_transfer_sz = W * 2;

    ESP_ERROR_CHECK(
        spi_bus_initialize(
            SPI2_HOST,
            &bus,
            SPI_DMA_CH_AUTO
        )
    );

    // -------------------------------------------------------------------------
    // Linkes Display
    // -------------------------------------------------------------------------

    spi_device_interface_config_t di = {};

    di.clock_speed_hz = 20000000;
    di.mode = 0;
    di.spics_io_num = HMB_EYE_CS_LEFT;
    di.queue_size = 1;

    ESP_ERROR_CHECK(
        spi_bus_add_device(
            SPI2_HOST,
            &di,
            &devL
        )
    );

    // -------------------------------------------------------------------------
    // Rechtes Display
    // -------------------------------------------------------------------------

    di.spics_io_num = HMB_EYE_CS_RIGHT;

    ESP_ERROR_CHECK(
        spi_bus_add_device(
            SPI2_HOST,
            &di,
            &devR
        )
    );

    // -------------------------------------------------------------------------
    // GC9D01 initialisieren
    // -------------------------------------------------------------------------

    initPanel(devL);
    initPanel(devR);

    // -------------------------------------------------------------------------
    // Eye Task starten
    // -------------------------------------------------------------------------

    BaseType_t result = xTaskCreate(
        task,
        "hmb_eye",
        6144,
        nullptr,
        2,
        nullptr
    );

    if(result != pdPASS){
        ESP_LOGE(TAG, "Could not create DualEye task");
        return;
    }

    ESP_LOGI(TAG, "DualEye ready");
}

void HmbDualEye::SetState(DeviceState state){
    state_ = state;
    eyeState = state;
}

#else

// ============================================================================
// DualEye deaktiviert
// ============================================================================

void HmbDualEye::Init(){
}

void HmbDualEye::SetState(DeviceState state){
    state_ = state;
}

#endif