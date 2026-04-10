#include "um_storage.h"
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
    const char *dirs[] = {
        UM_SD_DIR_MESSAGES,
        UM_SD_DIR_OTA,
        UM_SD_DIR_LOGS,
    };
    for (auto d : dirs) {
        if (!SD.exists(d)) {
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
