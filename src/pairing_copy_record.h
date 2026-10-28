// The copy of the pairing record outside the file system, and the choice of its flash page. On
// 2026-10-24 a damaged file system took the pairing with it; the copy lives on two pages of
// their own (pairing_copy.h) and brings it back. No hardware dependencies (tested on the PC).
//
// Copy, at the start of its page, multi-byte values little-endian:
//   0..3    magic "GXPC"
//   4       format version, 1
//   5..7    0, reserved
//   8..11   sequence number: 1 for the first copy, one more with every copy written
//   12..43  the pairing record as in /gpxcam/pairing.bin (pairing_record.h, with its own CRC)
//   44..47  CRC-32 (pairing_record::crc32()) of bytes 0..43
// The rest of the page is erased (0xFF).
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "pairing_record.h"

namespace pairing_copy_record {

constexpr size_t SIZE = 48; // a multiple of 4: flash is written in words
constexpr size_t PAGES = 2;

// What a page holds:
//   Empty      - erased, all bytes 0xFF;
//   Copy       - a valid copy (sequence and record set);
//   Broken     - our magic, but not a valid copy: a write cut off; free to be written again;
//   Meshtastic - a page of Meshtastic's node cache: Meshtastic 2.8 and later keep their
//                WarmNodeStore ring on 0xEA000-0xECFFF of the nRF52840 (src/mesh/WarmNodeStore.h,
//                from commit 79a7dcc46), each page starting with the magic "WRNG", "WRN2" or
//                "WRN3" (WARM_RING_MAGIC*, WarmNodeStore.cpp). After a return from Meshtastic gpxcam
//                does not need it, so the page may be written; Meshtastic drops our pages as
//                corrupt and rebuilds its cache (ringReadHeader(), load());
//   Foreign    - anything else: data of something else, never erased or written.
enum class Kind : uint8_t { Empty, Copy, Broken, Meshtastic, Foreign };

struct Page {
    Kind kind;
    uint32_t sequence;              // for Copy
    pairing_record::Record record; // for Copy
};

void encode(const pairing_record::Record &record, uint32_t sequence, uint8_t out[SIZE]);

// The whole page (size bytes, at least SIZE) is looked at: an empty page must be all 0xFF.
Page classify(const uint8_t *page, size_t size);

// A page may be written: Empty, Copy, Broken or Meshtastic.
bool usable(const Page &page);

// The index of the newest Copy (the highest sequence), or -1 if there is none.
int newest(const Page pages[PAGES]);

// Where the next copy goes, or -1 if no page is usable: the usable page that does not hold the
// newest copy (the older one, an empty or a broken one), so the newest stays while the other is
// written. With one usable page only, that page, also when it holds the newest copy.
int writeTarget(const Page pages[PAGES]);

// The sequence number of the next copy: one more than the newest, 1 for the first.
uint32_t nextSequence(const Page pages[PAGES]);

const char *kindText(Kind kind); // "empty", "copy", "broken copy", "Meshtastic node cache", "other data"

} // namespace pairing_copy_record
