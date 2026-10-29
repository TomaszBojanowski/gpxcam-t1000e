//! The commands the app lets through to the card: the same as the page (docs/index.html
//! allowedCommand(), test/commands.json). The page checks them first; this is the second check,
//! so that nothing in the window can send the card anything else ('dfu', 'off', 'link off').

// ZONE_NAMES: every zone and alias the page offers, sorted; LEGACY_NAMES, for the tests: the old
// POSIX-style names the card knows but the page does not offer (build.rs, from docs/zones.js).
include!(concat!(env!("OUT_DIR"), "/zones.rs"));

/// Sent exactly as written.
pub const FIXED: [&str; 18] = [
    "v",
    "status",
    "pair",
    "pair off",
    "pair older",
    "pair prepare",
    "p",
    "unpair",
    "clock on",
    "clock off",
    "updates normal",
    "updates frequent",
    "sound loud",
    "sound quiet",
    "sound off",
    "log",
    "diag scan",
    "tz",
];

/// True for a fixed command, and for "tz NAME" with NAME a zone the page offers, spelled as the card's
/// table spells it.
pub fn allowed(command: &str) -> bool {
    FIXED.contains(&command)
        || command
            .strip_prefix("tz ")
            .is_some_and(|name| ZONE_NAMES.binary_search(&name).is_ok())
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::Value;

    // The vectors the page's tests use too.
    const VECTORS: &str = include_str!("../../../test/commands.json");
    // The card's own table, made by tools/gen_tz_table.py from the same tz database as docs/zones.js.
    const CARD_TABLE: &str = include_str!("../../../src/tz_table_data.inc");

    fn list(vectors: &Value, key: &str) -> Vec<String> {
        vectors[key]
            .as_array()
            .unwrap()
            .iter()
            .map(|command| command.as_str().unwrap().to_string())
            .collect()
    }

    #[test]
    fn the_fixed_commands_are_those_of_the_vectors() {
        let vectors: Value = serde_json::from_str(VECTORS).unwrap();
        let mut fixed = list(&vectors, "fixed");
        let mut ours: Vec<String> = FIXED.iter().map(|command| command.to_string()).collect();
        fixed.sort();
        ours.sort();
        assert_eq!(ours, fixed);
    }

    #[test]
    fn the_vectors_pass_or_are_refused() {
        let vectors: Value = serde_json::from_str(VECTORS).unwrap();
        for command in list(&vectors, "fixed").iter().chain(list(&vectors, "allowed").iter()) {
            assert!(allowed(command), "{command:?} should pass");
        }
        for command in list(&vectors, "refused") {
            assert!(!allowed(&command), "{command:?} should be refused");
        }
    }

    #[test]
    fn the_app_lets_through_the_zones_of_the_card_but_the_legacy_names() {
        let start = CARD_TABLE
            .find("constexpr char ZONE_NAMES[] =")
            .expect("ZONE_NAMES in the card's table");
        let end = start + CARD_TABLE[start..].find(';').expect("the end of ZONE_NAMES");
        let mut card: Vec<&str> = CARD_TABLE[start..end]
            .lines()
            .filter_map(|line| line.trim().strip_prefix('"')?.strip_suffix("\\0\""))
            .collect();
        card.sort_unstable();
        let mut known: Vec<&str> = ZONE_NAMES.iter().chain(LEGACY_NAMES).copied().collect();
        known.sort_unstable();
        assert_eq!(known, card);
        for name in LEGACY_NAMES {
            assert!(!allowed(&format!("tz {name}")), "{name} is not offered");
        }
        assert!(
            ZONE_NAMES.windows(2).all(|pair| pair[0] < pair[1]),
            "sorted, for the binary search"
        );
        for name in ZONE_NAMES.iter().filter(|name| name.to_lowercase() != **name) {
            assert!(!allowed(&format!("tz {}", name.to_lowercase())), "{name} in lower case");
        }
    }
}
