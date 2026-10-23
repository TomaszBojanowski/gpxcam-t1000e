// Small records of the card, each in its own file of the internal file system (InternalFS).
// A record is written to a temporary file that is renamed over it, which littlefs 1.7 does
// atomically, also on power loss, replacing the old file (littlefs README, lfs.h lfs_rename());
// then it is read back, because File::close() reports no errors. Use only when Bluefruit
// mounted the file system (see tz_store.h). Loop task only.
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
