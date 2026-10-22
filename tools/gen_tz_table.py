#!/usr/bin/env python3
"""Generates, from the tz database of IANA in a pinned version (the same version gives the same
files on any computer and any day):
- src/tz_table_data.inc: the time zone table of the 'tz' command;
- test/test_posix_tz/zone_vectors.inc: the test vectors of src/posix_tz.cpp;
- the zone data of the time zone page, docs/timezone/index.html, between its TZ_DATA markers.

Source: the release archives tzdata<version>.tar.gz and tzcode<version>.tar.gz from
https://data.iana.org/time-zones/releases/, downloaded once into ~/.cache/gpxcam-t1000e and
checked against the SHA-256 below at every run. In a temporary directory the script builds zic
from tzcode and compiles the data with it as IANA's Makefile does by default (DATAFORM=main,
the files of TDATA_TO_CHECK, no backzone).

The rule of a zone is the footer of its compiled TZif file (RFC 8536, section 3.3): the POSIX
TZ string that describes the zone after its last listed transition. Names: the Zone and Link
lines of the data files (a Link is an alias, e.g. Poland for Europe/Warsaw). The check compares,
for every zone, the rule alone (evaluated by the C library) with the full compiled database
(Python's zoneinfo) from the release date of the version (in its NEWS file) to the end of 2031:
the card sets the present time, so earlier changes of a zone's law do not matter (in 2026:
Morocco, and parts of Canada keeping summer time). A difference means that the zone's changes
in these years are listed as single transitions the rule does not describe.

The test vectors give, for one zone of every rule, the UTC offset from the full database one
second before and at every change of 2026-2031 (for a rule without summer time: on
2027-07-01).

The page data: every zone of the card with the countries it covers and IANA's comment
(zone1970.tab), the aliases, and for every country its name (iso3166.tab) and its zones as
named for it (zone.tab: Denmark - Europe/Copenhagen, an alias of Europe/Berlin).

Usage: python3 tools/gen_tz_table.py [--check]
--check writes nothing and fails if a generated file is not up to date.
Needs Python 3.9+, a C compiler (cc) and make; the network on the first run.

A new version of the tz database:
1. Download tzdata<version>.tar.gz, tzcode<version>.tar.gz and their .asc signatures from the
   address above and check them: gpg --verify <archive>.asc <archive>. The tz releases are
   signed by Paul Eggert, key 7E37 92A9 D8AC F7D6 33BC 1588 ED97 E90E 62AA 7E34.
2. Put the version and the SHA-256 of both archives (sha256sum) below.
3. Run this script and pio test -e native, build the firmware, commit the regenerated files.
"""
import calendar
import datetime
import hashlib
import json
import os
import re
import subprocess
import sys
import tarfile
import tempfile
import time
import urllib.request
import zoneinfo

TZDB_VERSION = "2026e"
RELEASES = "https://data.iana.org/time-zones/releases/"
# SHA-256 of the archives; their PGP signatures were checked on 2026-10-22 (key above).
ARCHIVES = {
    f"tzdata{TZDB_VERSION}.tar.gz": "b26882805f26aac59d5b222978e6580484b834ccdc98be89df2f05a6dc53a652",
    f"tzcode{TZDB_VERSION}.tar.gz": "cc3d27ca2a0d8399504551b920970d80af83bfb9c216e8082a15491921935d54",
}
CACHE = os.path.join(os.environ.get("XDG_CACHE_HOME") or os.path.expanduser("~/.cache"), "gpxcam-t1000e")
DOWNLOAD_TIMEOUT_S = 60
# IANA's Makefile (2026e): TDATA_TO_CHECK = africa ... southamerica etcetera factory backward.
DATA_FILES = ["africa", "antarctica", "asia", "australasia", "europe", "northamerica", "southamerica",
              "etcetera", "factory", "backward"]

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
OUTPUT = os.path.join(ROOT, "src", "tz_table_data.inc")
VECTORS = os.path.join(ROOT, "test", "test_posix_tz", "zone_vectors.inc")
PAGE = os.path.join(ROOT, "docs", "timezone", "index.html")
PAGE_BEGIN = "// BEGIN TZ_DATA"
PAGE_END = "// END TZ_DATA"
VECTOR_YEARS = range(2026, 2032)
FIXED_OFFSET_MOMENT = calendar.timegm((2027, 7, 1, 0, 0, 0))
CHECK_STEP_S = 6 * 3600
SKIPPED = {"Factory"}  # a placeholder zone, not a place
NAME = re.compile(r"^[A-Za-z0-9/_+-]{1,40}$")
RULE = re.compile(r"^[A-Za-z0-9<>+\-:,./]{1,63}$")


def sha256(path):
    with open(path, "rb") as f:
        return hashlib.sha256(f.read()).hexdigest()


def archive(name):
    """The cached archive, downloaded first if missing; stops if its SHA-256 is not the pinned one."""
    path = os.path.join(CACHE, name)
    if not os.path.exists(path):
        os.makedirs(CACHE, exist_ok=True)
        print(f"downloading {RELEASES}{name}")
        with urllib.request.urlopen(RELEASES + name, timeout=DOWNLOAD_TIMEOUT_S) as answer:
            data = answer.read()
        if hashlib.sha256(data).hexdigest() != ARCHIVES[name]:  # never into the cache
            raise SystemExit(f"{RELEASES}{name}: SHA-256 {hashlib.sha256(data).hexdigest()}, "
                             f"expected {ARCHIVES[name]} - not used")
        with open(path + ".part", "wb") as f:
            f.write(data)
        os.replace(path + ".part", path)
    if sha256(path) != ARCHIVES[name]:
        raise SystemExit(f"{path}: SHA-256 {sha256(path)}, expected {ARCHIVES[name]} - not used")
    return path


def extract(path, directory):
    """IANA's archives hold plain files without directories; anything else is refused."""
    with tarfile.open(path) as tar:
        for member in tar.getmembers():
            if not member.isfile() or "/" in member.name or member.name.startswith("."):
                raise SystemExit(f"{path}: unexpected entry {member.name!r}")
        # The "data" filter (Python 3.12+, and later 3.9-3.11 updates) refuses unsafe entries too.
        tar.extractall(directory, **({"filter": "data"} if hasattr(tarfile, "data_filter") else {}))


def release_date(build):
    """The day the version was released, from the first "Release" line of NEWS, at 00:00 UTC."""
    with open(os.path.join(build, "NEWS"), encoding="utf-8") as f:
        for line in f:
            found = re.match(r"Release (\S+) - (\d{4})-(\d{2})-(\d{2}) ", line)
            if found:
                if found.group(1) != TZDB_VERSION:
                    break
                return calendar.timegm((int(found.group(2)), int(found.group(3)), int(found.group(4)), 0, 0, 0))
    raise SystemExit(f"NEWS: no release date of {TZDB_VERSION}")


def compile_database(build):
    """Builds zic and compiles the data; returns the directory with the TZif files."""
    for name in ARCHIVES:
        extract(archive(name), build)
    with open(os.path.join(build, "version"), encoding="ascii") as f:
        if f.read().strip() != TZDB_VERSION:
            raise SystemExit(f"the archives are not version {TZDB_VERSION}")
    subprocess.run(["make", "-s", "zic"], cwd=build, check=True, stdout=subprocess.DEVNULL)
    tzdir = os.path.join(build, "zoneinfo")
    subprocess.run([os.path.join(build, "zic"), "-d", tzdir] + DATA_FILES, cwd=build, check=True)
    return tzdir


def zones_and_aliases(build):
    """Zone names and {alias: target} from the Zone and Link lines of the data files."""
    zones, aliases = [], {}
    for data_file in DATA_FILES:
        with open(os.path.join(build, data_file), encoding="utf-8") as f:
            for line in f:
                fields = line.split("#", 1)[0].split()
                if fields and fields[0] == "Zone":
                    zones.append(fields[1])
                elif fields and fields[0] == "Link":
                    aliases[fields[2]] = fields[1]  # Link <target> <alias>
    return zones, aliases


def posix_rule(tzdir, zone):
    with open(os.path.join(tzdir, zone), "rb") as f:
        data = f.read()
    if not data.startswith(b"TZif") or not data.endswith(b"\n"):
        raise SystemExit(f"{zone}: not a TZif file with a footer")
    return data.rstrip(b"\n").rsplit(b"\n", 1)[-1].decode("ascii")


def full_database(tzdir, zone):
    with open(os.path.join(tzdir, zone), "rb") as f:
        return zoneinfo.ZoneInfo.from_file(f, key=zone)


def year_start(year):
    return calendar.timegm((year, 1, 1, 0, 0, 0))


def check_rules(tzdir, rules, start):
    """Zones whose rule alone gives another UTC offset than the full database from start to
    the end of the vector years."""
    differing = []
    for zone, rule in rules.items():
        os.environ["TZ"] = rule
        time.tzset()
        full = full_database(tzdir, zone)
        for t in range(start, year_start(VECTOR_YEARS.stop), CHECK_STEP_S):
            if utc_offset(full, t) != time.localtime(t).tm_gmtoff:
                differing.append(zone)
                break
    return differing


def utc_offset(tz, t):
    return int(datetime.datetime.fromtimestamp(t, tz).utcoffset().total_seconds())


def utc_text(t):
    return datetime.datetime.fromtimestamp(t, datetime.timezone.utc).strftime("%Y-%m-%d %H:%M:%S")


def changes(tz, years):
    """The moments at which the zone's UTC offset changes in these years."""
    found = []
    for t in range(year_start(years.start), year_start(years.stop), 3600):
        if utc_offset(tz, t) != utc_offset(tz, t + 3600):
            before, after = t, t + 3600
            while after - before > 1:
                middle = (before + after) // 2
                if utc_offset(tz, middle) == utc_offset(tz, before):
                    before = middle
                else:
                    after = middle
            found.append(after)
    return found


def vectors_text(tzdir, zones, rules, version):
    vectors = []
    for rule in sorted(set(rules[z] for z in zones)):
        zone = min((z for z in zones if rules[z] == rule), key=sort_key)
        tz = full_database(tzdir, zone)
        if "," in rule:
            moments = [m + d for m in changes(tz, VECTOR_YEARS) for d in (-1, 0)]
        else:
            moments = [FIXED_OFFSET_MOMENT]
        vectors += [f'    {{"{zone}", {t}, {utc_offset(tz, t)}}}, // {utc_text(t)} UTC' for t in moments]
    lines = [
        "// Generated by tools/gen_tz_table.py from the tz database " + version + " - do not edit.",
        "// For one zone of every rule in src/tz_table_data.inc: its UTC offset from the full database",
        f"// one second before and at every change of {VECTOR_YEARS.start}-{VECTOR_YEARS.stop - 1}, or on "
        + utc_text(FIXED_OFFSET_MOMENT)[:10] + " for a",
        "// rule without summer time.",
        "",
        "struct ZoneVector {",
        "    const char *zone;",
        "    int64_t unixSeconds;",
        "    int32_t utcOffsetSeconds;",
        "};",
        "",
        "const ZoneVector ZONE_VECTORS[] = {",
    ] + vectors + ["};"]
    print(f"{os.path.relpath(VECTORS)}: {len(vectors)} test vectors")
    return "\n".join(lines) + "\n"


def sort_key(name):
    return (name.lower(), name)


def c_string(text):
    return '"' + text.replace("\\", "\\\\").replace('"', '\\"') + '\\0"'


def table_rules(tzdir, zones, aliases):
    """{name: POSIX rule} for every zone and alias, checked for what the card accepts."""
    rules = {z: posix_rule(tzdir, z) for z in zones}
    for alias, target in aliases.items():
        if alias in SKIPPED:
            continue
        if target not in rules:
            raise SystemExit(f"alias {alias} points to {target}, which is not a zone of the table")
        rules.setdefault(alias, rules[target])
    for name, rule in rules.items():
        if not NAME.match(name) or not RULE.match(rule):
            raise SystemExit(f"unexpected characters: {name!r} {rule!r}")
    if len({n.lower() for n in rules}) != len(rules):
        raise SystemExit("names that differ only in case: the card looks names up ignoring case")
    return rules


def table_text(zones, rules, version, checked):
    names = sorted(rules, key=sort_key)
    unique_rules = sorted(set(rules.values()))
    rule_index = {r: i for i, r in enumerate(unique_rules)}
    if len(unique_rules) > 255:
        raise SystemExit("more than 255 rules: the index needs 16 bits")
    out = [
        "// Generated by tools/gen_tz_table.py from the tz database " + version + " - do not edit.",
        f"// {len(zones)} zones and {len(names) - len(zones)} aliases, {len(unique_rules)} distinct POSIX rules.",
        f"// Checked against the full database {checked}: no differences.",
        "",
        f'constexpr char TZDATA_VERSION[] = "{version}";',
        f"constexpr size_t ZONE_COUNT = {len(names)};",
        f"constexpr size_t RULE_COUNT = {len(unique_rules)};",
        "",
        "// Zone names, sorted ignoring case, each ended by a NUL.",
        "constexpr char ZONE_NAMES[] =",
    ]
    out += [f"    {c_string(n)}" for n in names]
    out[-1] += ";"
    out += ["", "// For every zone in ZONE_NAMES: its rule in RULES.", "constexpr uint8_t ZONE_RULES[ZONE_COUNT] = {"]
    indexes = [str(rule_index[rules[n]]) for n in names]
    out += ["    " + ", ".join(indexes[i:i + 20]) + "," for i in range(0, len(indexes), 20)]
    out += ["};", "", "// POSIX TZ rules, each ended by a NUL.", "constexpr char RULES[] ="]
    out += [f"    {c_string(r)}" for r in unique_rules]
    out[-1] += ";"
    print(f"{os.path.relpath(OUTPUT)}: tz database {version}, {len(zones)} zones, "
          f"{len(names) - len(zones)} aliases, {len(unique_rules)} rules, "
          f"{sum(len(n) + 2 for n in names) + sum(len(r) + 1 for r in unique_rules)} bytes of data")
    return "\n".join(out) + "\n"


def read_tab(build, name):
    """The rows of a .tab file of the tz database as lists of fields, without comment lines."""
    with open(os.path.join(build, name), encoding="utf-8") as f:
        return [line.rstrip("\n").split("\t") for line in f if line.strip() and not line.startswith("#")]


def page_data_text(build, rules, zones, aliases, version):
    """The TZ_DATA block of the page: a JavaScript constant, one entry per line."""
    covered = {}  # zone: [country codes, comment]
    for codes, _, zone, *comment in read_tab(build, "zone1970.tab"):
        covered[zone] = [codes.split(","), comment[0] if comment else ""]
    country_names = {code: name for code, name in read_tab(build, "iso3166.tab")}
    country_zones = {}  # code: [[zone as named for the country, comment], ...]
    for code, _, zone, *comment in read_tab(build, "zone.tab"):
        country_zones.setdefault(code, []).append([zone, comment[0] if comment else ""])
    named = list(covered) + [zone for entries in country_zones.values() for zone, _ in entries]
    if any(zone not in rules for zone in named):
        raise SystemExit(f"zone1970.tab or zone.tab names zones the card does not know: "
                         f"{sorted(z for z in named if z not in rules)}")
    lines = [
        f"{PAGE_BEGIN} - generated by tools/gen_tz_table.py from the tz database {version}, do not edit.",
        "// zones: [name, country codes, comment] for every zone of the card (zone1970.tab; none for Etc/...);",
        "// aliases: alias: zone; countries: code: [name (iso3166.tab), [[zone as named for the country,",
        "// comment], ...] (zone.tab)].",
        "const TZ_DATA = {",
        f'"version": {json.dumps(version)},',
        '"zones": [',
    ]
    lines += [json.dumps([z] + covered.get(z, [[], ""]), ensure_ascii=False) + "," for z in sorted(zones, key=sort_key)]
    lines += ["],", '"aliases": {']
    lines += [f"{json.dumps(a)}: {json.dumps(aliases[a])}," for a in sorted(aliases, key=sort_key) if a in rules]
    lines += ["},", '"countries": {']
    lines += [f"{json.dumps(code)}: {json.dumps([country_names[code], country_zones[code]], ensure_ascii=False)},"
              for code in sorted(country_zones)]
    lines += ["},", "};", PAGE_END]
    print(f"{os.path.relpath(PAGE)}: {len(zones)} zones, {len(country_zones)} countries")
    return "\n".join(lines)


def page_text(block):
    """The page with its TZ_DATA block replaced."""
    with open(PAGE, encoding="utf-8") as f:
        text = f.read()
    start, end = text.find(PAGE_BEGIN), text.find(PAGE_END)
    if start < 0 or end < start:
        raise SystemExit(f"{PAGE}: no {PAGE_BEGIN} ... {PAGE_END} block")
    return text[:start] + block + text[end + len(PAGE_END):]


def up_to_date(path, text, check):
    """True if path already holds text; otherwise writes it, unless only checking."""
    try:
        with open(path, encoding="utf-8") as f:
            if f.read() == text:
                return True
    except FileNotFoundError:
        pass
    if not check:
        with open(path, "w", encoding="utf-8") as f:
            f.write(text)
    return False


def main():
    if sys.argv[1:] not in ([], ["--check"]):
        raise SystemExit("usage: python3 tools/gen_tz_table.py [--check]")
    check = sys.argv[1:] == ["--check"]
    with tempfile.TemporaryDirectory() as build:
        tzdir = compile_database(build)
        zones, aliases = zones_and_aliases(build)
        zones = [z for z in zones if z not in SKIPPED]
        rules = table_rules(tzdir, zones, aliases)
        released = release_date(build)
        checked = f"from the release of {TZDB_VERSION} ({utc_text(released)[:10]}) to {VECTOR_YEARS.stop - 1}"
        differing = check_rules(tzdir, {z: rules[z] for z in zones}, released)
        for zone in differing:
            print(f"{zone}: the rule '{rules[zone]}' differs from the full database {checked}")
        if differing:
            raise SystemExit("the card would give these zones a wrong time - nothing written")
        outputs = {
            OUTPUT: table_text(zones, rules, TZDB_VERSION, checked),
            VECTORS: vectors_text(tzdir, zones, rules, TZDB_VERSION),
            PAGE: page_text(page_data_text(build, rules, zones, aliases, TZDB_VERSION)),
        }
    stale = [path for path, text in outputs.items() if not up_to_date(path, text, check)]
    for path in stale:
        print(f"{os.path.relpath(path)}: {'not up to date' if check else 'written'}")
    if check and stale:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
