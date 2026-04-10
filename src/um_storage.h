#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "config.h"

// -------------------------------------------------------
// um_storage — thin SD card helper layer
//
// Call um_storage_init() once in setup() after instance.begin().
// All paths must start with UM_SD_ROOT ("/sd").
// -------------------------------------------------------

// True when SD card was successfully mounted at boot.
// Checked before every SD operation.
extern bool um_sd_online;

// Mount the SD card and create the standard directory tree.
// Returns true on success.  Safe to call if SD is already up.
bool um_storage_init();

// Returns true if a file or directory exists at path.
bool um_storage_exists(const char *path);

// Create a directory (and any missing parents).
// Returns true on success or if the directory already exists.
bool um_storage_mkdir(const char *path);

// Write data to a file, overwriting any existing content.
// Returns true on success.
bool um_storage_write(const char *path, const uint8_t *data, size_t len);

// Convenience overload for null-terminated strings.
bool um_storage_write_str(const char *path, const char *text);

// Append text (with optional newline) to a file.
// Returns true on success.
bool um_storage_append(const char *path, const char *text, bool newline = true);

// Read entire file into a caller-supplied buffer.
// At most buf_len-1 bytes are read; the buffer is always null-terminated.
// Returns number of bytes read, or -1 on error.
int  um_storage_read(const char *path, char *buf, size_t buf_len);

// Delete a file.  Returns true on success or if the file didn't exist.
bool um_storage_remove(const char *path);

// Disk info — filled by um_storage_init(), updated on request.
uint64_t um_storage_total_bytes();
uint64_t um_storage_used_bytes();
