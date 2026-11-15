#pragma once
// HMB|TEC XiaoZhi API-Labor V0.9.22 - reine Messinstrumentierung.
#include <esp_timer.h>
#include <esp_log.h>
#include <atomic>
#include <cstdint>
namespace HmbApiLab {
inline std::atomic<int64_t> request_us{0};
inline std::atomic<uint32_t> received_audio{0};
inline std::atomic<uint32_t> request_id{0};
inline std::atomic<bool> first_audio{false};
inline int64_t now_ms() { return esp_timer_get_time()/1000; }
inline void event(const char* name) {
  const int64_t base=request_us.load();
  ESP_LOGI("HMB_API_LAB", "id=%lu event=%s t=%lldms dt=%lldms audio=%lu",
    (unsigned long)request_id.load(), name, (long long)now_ms(),
    (long long)(base ? (esp_timer_get_time()-base)/1000 : -1),
    (unsigned long)received_audio.load());
}
inline void request(const char* prompt) {
  request_id.fetch_add(1); request_us.store(esp_timer_get_time());
  received_audio.store(0); first_audio.store(false);
  ESP_LOGI("HMB_API_LAB", "id=%lu prompt=%s", (unsigned long)request_id.load(), prompt);
  event("API_REQUEST");
}
inline void audio_packet() {
  received_audio.fetch_add(1);
  if (!first_audio.exchange(true)) event("AUDIO_FIRST_RX");
}
inline void status() {
  ESP_LOGI("HMB_API_LAB", "STATUS id=%lu packets=%lu first_audio=%d elapsed_ms=%lld",
    (unsigned long)request_id.load(), (unsigned long)received_audio.load(),
    (int)first_audio.load(),
    (long long)(request_us.load() ? (esp_timer_get_time()-request_us.load())/1000 : -1));
}
inline void reset() { request_us.store(0); received_audio.store(0); first_audio.store(false); request_id.store(0); event("RESET"); }
}
