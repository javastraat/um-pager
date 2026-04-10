#include "um_storage.h"
#include <string.h>
#include <time.h>
#ifndef SIM_BUILD
#include <Arduino.h>
#include <LilyGoLib.h>
#include <SD.h>
#endif

bool um_sd_online = false;

// -------------------------------------------------------
// Init
// -------------------------------------------------------
bool um_storage_init()
{
#ifdef SIM_BUILD
    um_sd_online = false;   // no SD in the desktop simulator
    return false;
#else
    // LilyGoLib already called installSD() inside begin().
    // Check if it succeeded by testing the card type.
    if (SD.cardType() == CARD_NONE) {
        um_sd_online = false;
        Serial.println("[SD] No card detected");
        return false;
    }

    um_sd_online = true;
    Serial.printf("[SD] Card online, %.1f MB total\n",
                  (double)SD.totalBytes() / (1024.0 * 1024.0));

    // Ensure standard directory tree exists
    // Create base dir first, then subdirs
    const char *dirs[] = {
        UM_SD_BASE_DIR,
        UM_SD_DIR_MESSAGES,
        UM_SD_DIR_OTA,
        UM_SD_DIR_LOGS,
    };
    for (auto d : dirs) {
        File f = SD.open(d);
        bool exists = f && f.isDirectory();
        if (f) f.close();
        if (!exists) {
            if (SD.mkdir(d)) {
                Serial.printf("[SD] Created %s\n", d);
            } else {
                Serial.printf("[SD] Warning: could not create %s\n", d);
            }
        }
    }
    return true;
#endif
}

// -------------------------------------------------------
// Helpers
// -------------------------------------------------------
bool um_storage_exists(const char *path)
{
#ifdef SIM_BUILD
    return false;
#else
    if (!um_sd_online) return false;
    return SD.exists(path);
#endif
}

bool um_storage_mkdir(const char *path)
{
#ifdef SIM_BUILD
    return false;
#else
    if (!um_sd_online) return false;
    if (SD.exists(path)) return true;
    return SD.mkdir(path);
#endif
}

bool um_storage_write(const char *path, const uint8_t *data, size_t len)
{
#ifdef SIM_BUILD
    return false;
#else
    if (!um_sd_online) return false;
    File f = SD.open(path, FILE_WRITE);
    if (!f) return false;
    size_t written = f.write(data, len);
    f.close();
    return written == len;
#endif
}

bool um_storage_write_str(const char *path, const char *text)
{
    return um_storage_write(path,
                            (const uint8_t *)text,
                            text ? strlen(text) : 0);
}

bool um_storage_append(const char *path, const char *text, bool newline)
{
#ifdef SIM_BUILD
    return false;
#else
    if (!um_sd_online || !text) return false;
    File f = SD.open(path, FILE_APPEND);
    if (!f) return false;
    f.print(text);
    if (newline) f.print('\n');
    f.close();
    return true;
#endif
}

int um_storage_read(const char *path, char *buf, size_t buf_len)
{
#ifdef SIM_BUILD
    (void)path; (void)buf; (void)buf_len;
    return -1;
#else
    if (!um_sd_online || !buf || buf_len == 0) return -1;
    File f = SD.open(path, FILE_READ);
    if (!f) return -1;
    size_t to_read = buf_len - 1;
    if ((size_t)f.size() < to_read) to_read = f.size();
    int n = f.read((uint8_t *)buf, to_read);
    f.close();
    if (n < 0) n = 0;
    buf[n] = '\0';
    return n;
#endif
}

bool um_storage_remove(const char *path)
{
#ifdef SIM_BUILD
    return false;
#else
    if (!um_sd_online) return false;
    if (!SD.exists(path)) return true;
    return SD.remove(path);
#endif
}

uint64_t um_storage_total_bytes()
{
#ifdef SIM_BUILD
    return 0;
#else
    if (!um_sd_online) return 0;
    return SD.totalBytes();
#endif
}

uint64_t um_storage_used_bytes()
{
#ifdef SIM_BUILD
    return 0;
#else
    if (!um_sd_online) return 0;
    return SD.usedBytes();
#endif
}

// -------------------------------------------------------
// Message inbox
// -------------------------------------------------------
bool um_storage_save_message(uint32_t ric, uint8_t func,
                             const char *msg,
                             const uint8_t *from_mac)
{
#ifdef SIM_BUILD
    (void)ric; (void)func; (void)msg; (void)from_mac;
    return false;
#else
    if (!um_sd_online || !msg) return false;

    // ---- Deduplication ---------------------------------------------------
    // Simple djb2 hash of ric+msg; skip if same (ric,hash) seen recently.
    {
        uint32_t hash = 5381;
        for (const char *p = msg; *p; p++)
            hash = ((hash << 5) + hash) ^ (uint8_t)*p;
        hash ^= ric;

        struct DedupSlot { uint32_t hash; time_t ts; };
        static DedupSlot slots[UM_MSG_DEDUP_SLOTS] = {};
        static uint8_t   next = 0;

        time_t now_t = time(nullptr);
        for (int i = 0; i < UM_MSG_DEDUP_SLOTS; i++) {
            if (slots[i].hash == hash &&
                (now_t - slots[i].ts) <= UM_MSG_DEDUP_WINDOW_S) {
                Serial.printf("[MSG] Dedup suppressed ric=%lu\n", (unsigned long)ric);
                return false;
            }
        }
        slots[next] = { hash, now_t };
        next = (next + 1) % UM_MSG_DEDUP_SLOTS;
    }

    // Build timestamped filename using RTC (always available, even if unsynced).
    // Append a rolling counter to avoid collisions within the same second.
    static uint32_t seq = 0;
    seq++;
    char fname[72];
    struct tm t = {};
    time_t now = time(nullptr);
    localtime_r(&now, &t);
    snprintf(fname, sizeof(fname),
             UM_SD_DIR_MESSAGES "/%04d%02d%02d_%02d%02d%02d_%04lu_ric%lu.json",
             t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
             t.tm_hour, t.tm_min, t.tm_sec,
             (unsigned long)(seq & 0xFFFF),
             (unsigned long)ric);

    // Build compact JSON
    char json[512];
    if (from_mac) {
        snprintf(json, sizeof(json),
                 "{\"ric\":%lu,\"func\":%u,\"from\":\"%02X:%02X:%02X:%02X:%02X:%02X\""
                 ",\"msg\":\"%s\"}\n",
                 (unsigned long)ric, func,
                 from_mac[0], from_mac[1], from_mac[2],
                 from_mac[3], from_mac[4], from_mac[5],
                 msg);
    } else {
        snprintf(json, sizeof(json),
                 "{\"ric\":%lu,\"func\":%u,\"msg\":\"%s\"}\n",
                 (unsigned long)ric, func, msg);
    }

    File f = SD.open(fname, FILE_WRITE);
    if (!f) {
        Serial.printf("[MSG] Could not save %s\n", fname);
        return false;
    }
    f.print(json);
    f.close();
    Serial.printf("[MSG] Saved %s\n", fname);
    return true;
#endif
}
