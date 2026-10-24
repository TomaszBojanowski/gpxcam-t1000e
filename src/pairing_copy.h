// The copy of the pairing record on two flash pages of its own, 0xEC000 and 0xEB000, outside the
// file system (format and choice of page: pairing_copy_record.h). A damaged file system or one
// formatted with 'storage format yes' then leaves the card paired: pairing_store reads the copy.
//
// Why these pages (the flash map in docs/HARDWARE.md): they lie in the application data area
// 0xEA000-0xF3FFF that the bootloader keeps (DFU_APP_DATA_RESERVED = 10 pages for the nRF52840,
// its Makefile), below the file system 0xED000-0xF3FFF (InternalFileSystem.cpp in the core).
// The program ends below 0xEA000 (variants/t1000-e/nrf52840_s140_v7.ld), flashing writes only
// 0x1000-0xE9FFF (the bootloader's ghostfat.c, in_app_space()), and nothing in the core, its
// libraries or this program uses them. 0xEA000 stays free.
//
// What is on a page before the first copy is not known (the card came with other firmware), so
// a page with anything but 0xFF or our own copy on it is never erased or written; with one such
// page the copy uses the other one alone, with two there is no copy. Loop task only.
#pragma once

#include "pairing_record.h"

namespace pairing_copy {

// Reads both pages and logs what they hold. Call in setup(); it only reads.
void begin();

// The newest valid copy; false if there is none.
bool read(pairing_record::Record *record);

// Writes the record as the next copy, on the older usable page (erase, program, read back).
// False, logged, when no page may be written or the write fails.
bool write(const pairing_record::Record &record);

// 'p': what the two pages hold.
void logState();

} // namespace pairing_copy
