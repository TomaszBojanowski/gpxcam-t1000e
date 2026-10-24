#include "pairing_copy.h"

#include <Arduino.h>
#include <flash/flash_nrf5x.h>
#include <stdio.h>
#include <string.h>

#include "log.h"
#include "pairing_copy_record.h"

namespace {

using pairing_copy_record::Kind;
using pairing_copy_record::Page;

// The first copy goes to PAGE_ADDRESS[0]. See pairing_copy.h for why these two.
constexpr uint32_t PAGE_ADDRESS[pairing_copy_record::PAGES] = {0xEC000, 0xEB000};
constexpr uint32_t APP_DATA_START = 0xEA000;   // USER_FLASH_END of the bootloader
constexpr uint32_t FILE_SYSTEM_START = 0xED000; // LFS_FLASH_ADDR of the core
static_assert(PAGE_ADDRESS[0] >= APP_DATA_START && PAGE_ADDRESS[0] + FLASH_NRF52_PAGE_SIZE <= FILE_SYSTEM_START,
              "between the program and the file system");
static_assert(PAGE_ADDRESS[1] >= APP_DATA_START && PAGE_ADDRESS[1] + FLASH_NRF52_PAGE_SIZE <= FILE_SYSTEM_START,
              "between the program and the file system");
static_assert(PAGE_ADDRESS[0] % FLASH_NRF52_PAGE_SIZE == 0 && PAGE_ADDRESS[1] % FLASH_NRF52_PAGE_SIZE == 0,
              "whole pages");

Page pages[pairing_copy_record::PAGES] = {};

const uint8_t *pageBytes(size_t index)
{
    return reinterpret_cast<const uint8_t *>(PAGE_ADDRESS[index]); // flash is mapped for reading
}

void readPages()
{
    for (size_t i = 0; i < pairing_copy_record::PAGES; i++)
        pages[i] = pairing_copy_record::classify(pageBytes(i), FLASH_NRF52_PAGE_SIZE);
}

// "0xEC000 copy 3", "0xEB000 empty", "0xEB000 other data".
void describe(size_t index, char *out, size_t size)
{
    if (pages[index].kind == Kind::Copy)
        snprintf(out, size, "0x%05lX copy %lu", (unsigned long)PAGE_ADDRESS[index],
                 (unsigned long)pages[index].sequence);
    else
        snprintf(out, size, "0x%05lX %s", (unsigned long)PAGE_ADDRESS[index],
                 pairing_copy_record::kindText(pages[index].kind));
}

// One line for the log: both pages, and what follows from them.
void logPages(bool keep)
{
    char first[32];
    char second[32];
    describe(0, first, sizeof(first));
    describe(1, second, sizeof(second));
    const bool firstUsable = pairing_copy_record::usable(pages[0]);
    const bool secondUsable = pairing_copy_record::usable(pages[1]);
    const char *note = firstUsable && secondUsable ? ""
                       : firstUsable              ? " - the other page is left alone, copies on this one only"
                       : secondUsable             ? " - the first page is left alone, copies on the other one only"
                                                  : " - both pages hold other data, left alone: no copy";
    if (keep)
        logger::line("pair copy: %s, %s%s", first, second, note);
    else
        logger::transient("pair copy: %s, %s%s", first, second, note);
}

} // namespace

namespace pairing_copy {

void begin()
{
    readPages();
    // Kept in the history when it is worth a look: a page left alone, a broken copy, or
    // Meshtastic's node cache that the copy takes back.
    bool unusual = false;
    for (const Page &page : pages)
        unusual = unusual || !pairing_copy_record::usable(page) || page.kind == Kind::Broken ||
                  page.kind == Kind::Meshtastic;
    logPages(unusual);
}

bool read(pairing_record::Record *record)
{
    const int index = pairing_copy_record::newest(pages);
    if (index < 0)
        return false;
    *record = pages[index].record;
    return true;
}

bool write(const pairing_record::Record &record)
{
    const int index = pairing_copy_record::writeTarget(pages);
    if (index < 0) {
        logger::line("pair copy: not written - both pages hold other data");
        return false;
    }
    const uint32_t address = PAGE_ADDRESS[index];
    const uint32_t sequence = pairing_copy_record::nextSequence(pages);
    uint8_t data[pairing_copy_record::SIZE];
    pairing_copy_record::encode(record, sequence, data);
    // Through the core's flash layer, as the file system: its page cache is shared and the
    // SoftDevice's flash events reach it. Erased first, so the rest of the page is 0xFF.
    const bool written = flash_nrf5x_erase(address) &&
                         flash_nrf5x_write(address, data, sizeof(data)) == (int)sizeof(data);
    flash_nrf5x_flush();
    readPages();
    const bool checked = written && pages[index].kind == Kind::Copy && pages[index].sequence == sequence &&
                         memcmp(pageBytes(index), data, sizeof(data)) == 0;
    if (!checked) {
        logger::line("pair copy: writing copy %lu to 0x%05lX failed", (unsigned long)sequence,
                     (unsigned long)address);
        return false;
    }
    logger::line("pair copy: copy %lu written to 0x%05lX", (unsigned long)sequence, (unsigned long)address);
    return true;
}

void logState()
{
    logPages(false);
}

} // namespace pairing_copy
