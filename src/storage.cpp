#include "storage.h"

#include <Arduino.h>
#include <InternalFileSystem.h>
#include <flash/flash_nrf5x.h>
#include <string.h>

#include "log.h"

using namespace Adafruit_LittleFS_Namespace;

namespace {

// The area of the core's file system: LFS_FLASH_ADDR and LFS_FLASH_TOTAL_SIZE in
// InternalFileSytem/src/InternalFileSystem.cpp (not exported); begin() checks the size against
// the core's configuration.
constexpr uint32_t AREA_START = 0xED000;
constexpr uint32_t AREA_PAGES = 7;
constexpr uint32_t AREA_SIZE = AREA_PAGES * FLASH_NRF52_PAGE_SIZE;
// The area ends where the bootloader starts (its linker/nrf52840.ld), so areaErased() and the
// format command cover all of it: 0xED000-0xF3FFF.
constexpr uint32_t BOOTLOADER_START = 0xF4000;
static_assert(AREA_START + AREA_SIZE == BOOTLOADER_START, "the whole file system area");
constexpr uint32_t ERASED_WORD = 0xFFFFFFFF;

// The file system in RAM that Bluefruit gets when the one in flash does not mount: room for the
// superblock, the root and Bluefruit's two bond directories (bond_init()), two blocks each.
constexpr lfs_size_t RAM_BLOCK_SIZE = 128; // as the core's LFS_BLOCK_SIZE
constexpr lfs_size_t RAM_BLOCKS = 16;
constexpr lfs_size_t RAM_LOOKAHEAD = 32; // littlefs 1.7 needs a multiple of 32
uint8_t ramDisk[RAM_BLOCKS * RAM_BLOCK_SIZE];

int ramRead(const lfs_config *, lfs_block_t block, lfs_off_t off, void *buffer, lfs_size_t size)
{
    memcpy(buffer, &ramDisk[block * RAM_BLOCK_SIZE + off], size);
    return 0;
}

int ramProg(const lfs_config *, lfs_block_t block, lfs_off_t off, const void *buffer, lfs_size_t size)
{
    memcpy(&ramDisk[block * RAM_BLOCK_SIZE + off], buffer, size);
    return 0;
}

int ramErase(const lfs_config *, lfs_block_t block)
{
    memset(&ramDisk[block * RAM_BLOCK_SIZE], 0xFF, RAM_BLOCK_SIZE);
    return 0;
}

int ramSync(const lfs_config *)
{
    return 0;
}

lfs_config ramConfig = {};

enum class State { Ok, Formatted, Damaged, NoRamStandIn };
State state = State::Damaged;
bool damagedInside = false; // Damaged: it mounted, but a block of it cannot be used

// The core's file system writes 128-byte blocks, but its flash cache erases and programs the whole
// 4 KB page each time (flash_cache.c, flash_cache_flush()): a reset in that moment (power, watchdog)
// can leave both halves of a directory, or blocks a file points to, erased. lfs_mount() reads only the
// superblock, so such a file system mounts, and littlefs stops the card in an assert at the first use
// of an erased pointer (0xFFFFFFFF, lfs.c: LFS_ASSERT(block < block_count)), at every write.
// lfs_traverse() (littlefs 1.7) passes each block number to the callback before it reads the block and
// checks every directory's CRC, so it finds such damage without the assert.
struct BlockCheck {
    lfs_size_t blockCount;
    uint32_t visits;
};
// A file's skip pointers name some blocks twice; many more visits would be a loop in damaged metadata.
constexpr uint32_t VISITS_PER_BLOCK = 4;

int checkBlock(void *data, lfs_block_t block)
{
    BlockCheck *check = static_cast<BlockCheck *>(data);
    if (block >= check->blockCount || ++check->visits > VISITS_PER_BLOCK * check->blockCount)
        return LFS_ERR_CORRUPT;
    return 0;
}

// Every directory and every file's block list of the mounted file system can be read.
bool wholeFileSystemUsable()
{
    lfs_t *lfs = InternalFS._getFS();
    BlockCheck check = {lfs->cfg->block_count, 0};
    return lfs_traverse(lfs, checkBlock, &check) == 0;
}

bool areaErased()
{
    for (uint32_t address = AREA_START; address < AREA_START + AREA_SIZE; address += sizeof(uint32_t)) {
        if (*reinterpret_cast<const volatile uint32_t *>(address) != ERASED_WORD)
            return false;
    }
    return true;
}

// The base class's begin(): mounts, and never formats (InternalFileSystem::begin() would).
bool mount(lfs_config *config = nullptr)
{
    return InternalFS.Adafruit_LittleFS::begin(config);
}

bool mountRamStandIn()
{
    ramConfig.read = ramRead;
    ramConfig.prog = ramProg;
    ramConfig.erase = ramErase;
    ramConfig.sync = ramSync;
    ramConfig.read_size = RAM_BLOCK_SIZE;
    ramConfig.prog_size = RAM_BLOCK_SIZE;
    ramConfig.block_size = RAM_BLOCK_SIZE;
    ramConfig.block_count = RAM_BLOCKS;
    ramConfig.lookahead = RAM_LOOKAHEAD;
    if (mount(&ramConfig))
        return true;
    return InternalFS.format() && mount(); // format() and mount() use ramConfig now
}

} // namespace

namespace storage {

void begin()
{
    if (mount()) {
        if (wholeFileSystemUsable()) {
            state = State::Ok;
            return;
        }
        InternalFS.end(); // damaged inside: left as it is, as one that does not mount
        damagedInside = true;
        state = mountRamStandIn() ? State::Damaged : State::NoRamStandIn;
        return;
    }
    // The core's configuration, kept by lfs_mount() also when it failed (lfs_init()).
    const lfs_config *flash = InternalFS._getFS()->cfg;
    const bool sizeMatches = flash != nullptr && flash->block_size * flash->block_count == AREA_SIZE;
    if (sizeMatches && areaErased() && InternalFS.format() && mount()) {
        state = State::Formatted;
        return;
    }
    state = mountRamStandIn() ? State::Damaged : State::NoRamStandIn;
}

bool ready()
{
    return state == State::Ok || state == State::Formatted;
}

bool safeForBluefruit()
{
    return state != State::NoRamStandIn;
}

void logStart()
{
    if (state == State::Formatted)
        logger::line("storage: the file system area was empty - formatted");
    else if (!ready())
        logState();
}

void format()
{
    logger::line("storage: erasing 0x%05lX-0x%05lX and restarting - the pairing comes back from its copy",
                 (unsigned long)AREA_START, (unsigned long)(AREA_START + AREA_SIZE));
    for (uint32_t page = AREA_START; page < AREA_START + AREA_SIZE; page += FLASH_NRF52_PAGE_SIZE) {
        if (!flash_nrf5x_erase(page)) {
            logger::line("storage: erasing 0x%05lX failed - not restarted", (unsigned long)page);
            return;
        }
    }
    delay(100); // lets the log line leave
    NVIC_SystemReset();
}

void logState()
{
    switch (state) {
    case State::Ok:
        logger::transient("storage: ok (file system 0x%05lX-0x%05lX)", (unsigned long)AREA_START,
                          (unsigned long)(AREA_START + AREA_SIZE));
        return;
    case State::Formatted:
        logger::transient("storage: ok, formatted at this start (the area was empty)");
        return;
    case State::Damaged:
    case State::NoRamStandIn:
        // Kept in the history: it explains a card that forgot its settings.
        logger::line("storage: DAMAGED - the file system 0x%05lX-0x%05lX %s; left as it is%s",
                     (unsigned long)AREA_START, (unsigned long)(AREA_START + AREA_SIZE),
                     damagedInside ? "has a damaged block" : "does not mount",
                     state == State::NoRamStandIn ? ", BLE not started" : "");
        logger::line("storage: the card uses the default settings; the pairing comes from its copy, if any;");
        logger::line("storage: 'storage format yes' erases it (time zone, settings, battery log; the pairing "
                     "comes back from its copy)");
        return;
    }
}

const char *stateName()
{
    return ready() ? "ok" : "damaged";
}

} // namespace storage
