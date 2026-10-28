#include "scan_dump.h"

#include <stdio.h>
#include <string.h>

#include "camera_list.h" // addressTypeName
#include "lumix.h"       // parseAdvert (Panasonic manufacturer data)

namespace scan_dump {

namespace {

size_t lengthOf(const Advert &advert)
{
    return advert.length <= MAX_ADVERT_BYTES ? advert.length : MAX_ADVERT_BYTES;
}

lumix::Advert panasonicKind(const Advert &advert)
{
    lumix::Camera camera;
    return lumix::parseAdvert(advert.data, lengthOf(advert), &camera);
}

} // namespace

size_t formatAdvert(const Advert &advert, char *out, size_t size)
{
    if (out == nullptr || size == 0)
        return 0;
    const lumix::Advert kind = panasonicKind(advert);
    const char *what = kind == lumix::Advert::Camera          ? ", camera"
                       : kind == lumix::Advert::OtherPanasonic ? ", Panasonic"
                                                               : "";
    // The last address byte is address[0] (formatAddress prints most significant byte first).
    const bool response = advert.kind == advert_kind::Kind::ScanResponse;
    const int written = snprintf(out, size, "**:**:**:**:**:%02x %s %d dBm ch %u %s%s%s %u B%s", advert.address[0],
                                 camera_list::addressTypeName(advert.addressType), advert.rssi,
                                 (unsigned)advert.channel, response ? "rsp" : "adv", response ? "" : " ",
                                 response ? "" : advert_kind::name(advert.kind), (unsigned)lengthOf(advert), what);
    if (written < 0 || (size_t)written >= size) {
        out[0] = '\0';
        return 0;
    }
    return (size_t)written;
}

size_t formatBytes(const Advert &advert, char *out, size_t size)
{
    if (out == nullptr || size == 0)
        return 0;
    out[0] = '\0';
    const size_t length = lengthOf(advert);
    if (length == 0 || panasonicKind(advert) == lumix::Advert::None)
        return 0; // full bytes only for Panasonic manufacturer data
    static const char LABEL[] = "bytes:";
    static const char HEX[] = "0123456789abcdef";
    const size_t total = sizeof(LABEL) - 1 + 3 * length; // " xx" per byte
    if (total >= size)
        return 0;
    memcpy(out, LABEL, sizeof(LABEL) - 1);
    size_t pos = sizeof(LABEL) - 1;
    for (size_t i = 0; i < length; i++) {
        out[pos++] = ' ';
        out[pos++] = HEX[advert.data[i] >> 4];
        out[pos++] = HEX[advert.data[i] & 0x0F];
    }
    out[pos] = '\0';
    return pos;
}

} // namespace scan_dump
