// Small records of the card, each in its own file of the internal file system (InternalFS).
// A record is written to a temporary file, which is read back - File::close() reports no errors
// (Adafruit_LittleFS_File.h), and a failed close would leave it short - and only then renamed over
// the record: littlefs 1.7 renames atomically, replacing the old file (lfs.h lfs_rename()), so a
// write cut off in the record's own blocks leaves the old record. It does not protect from the loss
// of the 4 KB flash page around them (storage.cpp, docs/HARDWARE.md). The record is read back once
// more after the rename. Use only when storage::ready(). Loop task only.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace record_file {

constexpr const char *DIRECTORY = "/gpxcam";

// One block of the file system (LFS_BLOCK_SIZE in the core's InternalFileSystem.cpp).
constexpr size_t MAX_SIZE = 128;

// Reads the file at path into data, at most capacity bytes, and sets *length. False if the
// file is missing. A capacity one byte above the record's size shows a longer file.
bool read(const char *path, uint8_t *data, size_t capacity, size_t *length);

// Writes size bytes (at most MAX_SIZE) as the file at path, through tempPath in the same
// directory, and reads them back. True if the file then holds exactly these bytes.
bool write(const char *path, const char *tempPath, const uint8_t *data, size_t size);

} // namespace record_file
