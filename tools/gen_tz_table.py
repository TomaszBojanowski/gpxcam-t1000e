#!/usr/bin/env python3
"""Generates the time zone table of the 'tz' command, src/tz_table_data.inc, and the test
vectors of src/posix_tz.cpp, test/test_posix_tz/zone_vectors.inc, from the tz database of IANA
in a pinned version: the same version gives the same files on any computer and any day.

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

Usage: python3 tools/gen_tz_table.py
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
import os
import re
import subprocess
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


def write_vectors(tzdir, zones, rules, version):
    vectors = []
    for rule in sorted(set(rules[z] for z in zones)):
        zone = min((z for z in zones if rules[z] == rule), key=lambda n: (n.lower(), n))
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
    with open(VECTORS, "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
    return len(vectors)


def c_string(text):
    return '"' + text.replace("\\", "\\\\").replace('"', '\\"') + '\\0"'


def main():
    with tempfile.TemporaryDirectory() as build:
        tzdir = compile_database(build)
        zones, aliases = zones_and_aliases(build)
        write_files(tzdir, zones, aliases, TZDB_VERSION, release_date(build))


def write_files(tzdir, zones, aliases, version, released):
    zones = [z for z in zones if z not in SKIPPED]
    rules = {z: posix_rule(tzdir, z) for z in zones}
    for alias, target in aliases.items():
        if target in rules and alias not in SKIPPED:
            rules.setdefault(alias, rules[target])
    for name, rule in rules.items():
        if not NAME.match(name) or not RULE.match(rule):
            raise SystemExit(f"unexpected characters: {name!r} {rule!r}")

    names = sorted(rules, key=lambda n: (n.lower(), n))
    if len({n.lower() for n in names}) != len(names):
        raise SystemExit("names that differ only in case: the card looks names up ignoring case")
    unique_rules = sorted(set(rules.values()))
    rule_index = {r: i for i, r in enumerate(unique_rules)}
    if len(unique_rules) > 255:
        raise SystemExit("more than 255 rules: the index needs 16 bits")

    checked = f"from the release of {version} ({utc_text(released)[:10]}) to {VECTOR_YEARS.stop - 1}"
    differing = check_rules(tzdir, {z: rules[z] for z in zones}, released)
    for zone in differing:
        print(f"warning: {zone}: the rule '{rules[zone]}' differs from the full database {checked}")

    out = [
        "// Generated by tools/gen_tz_table.py from the tz database " + version + " - do not edit.",
        f"// {len(zones)} zones and {len(names) - len(zones)} aliases, {len(unique_rules)} distinct POSIX rules.",
        f"// Checked against the full database {checked}: "
        + ("no differences." if not differing else f"{len(differing)} zones differ."),
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
    with open(OUTPUT, "w", encoding="utf-8") as f:
        f.write("\n".join(out) + "\n")
    print(f"{os.path.relpath(OUTPUT)}: tz database {version}, {len(zones)} zones, "
          f"{len(names) - len(zones)} aliases, {len(unique_rules)} rules, "
          f"{sum(len(n) + 2 for n in names) + sum(len(r) + 1 for r in unique_rules)} bytes of data")
    count = write_vectors(tzdir, zones, rules, version)
    print(f"{os.path.relpath(VECTORS)}: {count} test vectors")


if __name__ == "__main__":
    main()
