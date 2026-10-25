#include "scan_dump.h"

#include <stdio.h>

#include "camera_list.h" // addressTypeName
#include "lumix.h"       // parseAdvert (Panasonic manufacturer data)

namespace scan_dump {

size_t formatAdvert(const Advert &advert, char *out, size_t size)
{
    if (out == nullptr || size == 0)
        return 0;
    const size_t length = advert.length <= MAX_ADVERT_BYTES ? advert.length : MAX_ADVERT_BYTES;
    lumix::Camera camera;
    const lumix::Advert kind = lumix::parseAdvert(advert.data, length, &camera);
    const char *what = kind == lumix::Advert::Camera          ? ", camera"
                       : kind == lumix::Advert::OtherPanasonic ? ", Panasonic"
                                                               : "";
    // The last address byte is address[0] (formatAddress prints most significant byte first).
    const int header = snprintf(out, size, "**:**:**:**:**:%02x %s %d dBm %s %u B%s", advert.address[0],
                                camera_list::addressTypeName(advert.addressType), advert.rssi,
                                advert.scanResponse ? "rsp" : "adv", (unsigned)length, what);
    if (header < 0 || (size_t)header >= size)
        return 0;
    size_t pos = (size_t)header;

    // Full bytes only for Panasonic manufacturer data.
    if (kind != lumix::Advert::None && length > 0) {
        static const char HEX[] = "0123456789abcdef";
        const char *label = ", bytes:";
        for (const char *c = label; *c; c++) {
            if (pos + 1 >= size)
                return 0;
            out[pos++] = *c;
        }
        for (size_t i = 0; i < length; i++) {
            if (pos + 4 >= size) // " xx" and the terminator
                return 0;
            out[pos++] = ' ';
            out[pos++] = HEX[advert.data[i] >> 4];
            out[pos++] = HEX[advert.data[i] & 0x0F];
        }
        out[pos] = '\0';
    }
    return pos;
}

} // namespace scan_dump
