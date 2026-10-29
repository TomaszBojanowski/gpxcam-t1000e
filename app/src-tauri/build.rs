//! Before the app is compiled: its ACL manifest, so the page may call only the app's own commands
//! (capabilities/default.json), and the list of time zone names the card knows, from the card
//! page's table (docs/zones.js, made by tools/gen_tz_table.py), so that 'tz NAME' passes here
//! exactly as on the page.
use std::{env, fs, path::Path};

const COMMANDS: &[&str] = &[
    "list_cards",
    "open_card",
    "send",
    "close_card",
    "register_older",
    "cancel_older",
];
const ZONES: &str = "../../docs/zones.js";
const TABLE_START: &str = "const TZ_DATA = ";
const TABLE_END: &str = "\n};"; // the file ends with it

fn main() {
    write_zone_names();
    let manifest = tauri_build::AppManifest::new().commands(COMMANDS);
    tauri_build::try_build(tauri_build::Attributes::new().app_manifest(manifest)).expect("tauri-build failed");
}

fn write_zone_names() {
    println!("cargo:rerun-if-changed={ZONES}");
    // A Windows checkout may have CRLF line ends (git's core.autocrlf); TABLE_END has LF.
    let text = fs::read_to_string(ZONES).expect("docs/zones.js").replace("\r\n", "\n");
    let start = text.find(TABLE_START).expect("TZ_DATA in docs/zones.js") + TABLE_START.len();
    let end = text.rfind(TABLE_END).expect("the end of TZ_DATA") + 2; // with its closing brace
    let table: serde_json::Value =
        serde_json::from_str(&without_trailing_commas(&text[start..end])).expect("TZ_DATA as JSON");
    let zones = table["zones"]
        .as_array()
        .expect("TZ_DATA.zones")
        .iter()
        .map(|zone| zone[0].as_str().expect("a name"));
    let aliases = table["aliases"]
        .as_object()
        .expect("TZ_DATA.aliases")
        .keys()
        .map(String::as_str);
    let mut names: Vec<&str> = zones.chain(aliases).collect();
    names.sort_unstable();
    names.dedup();
    let list: String = names.iter().map(|name| format!("    {name:?},\n")).collect();
    let code = format!(
        "/// Every zone and alias of the card's table, sorted.\npub const ZONE_NAMES: &[&str] = &[\n{list}];\n"
    );
    fs::write(Path::new(&env::var("OUT_DIR").expect("OUT_DIR")).join("zones.rs"), code).expect("zones.rs");
}

/// The table is a JavaScript object: JSON, but with a comma before some closing brackets.
fn without_trailing_commas(text: &str) -> String {
    let mut out = String::with_capacity(text.len());
    let (mut in_string, mut escaped) = (false, false);
    for c in text.chars() {
        if in_string {
            out.push(c);
            if escaped {
                escaped = false;
            } else if c == '\\' {
                escaped = true;
            } else if c == '"' {
                in_string = false;
            }
            continue;
        }
        if c == ']' || c == '}' {
            let kept = out.trim_end().len();
            if out[..kept].ends_with(',') {
                out.truncate(kept - 1);
            }
        } else if c == '"' {
            in_string = true;
        }
        out.push(c);
    }
    out
}
