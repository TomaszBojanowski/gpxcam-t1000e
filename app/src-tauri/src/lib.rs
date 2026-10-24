//! gpxcam card: the card page (docs/index.html) in its own window, for computers without Chrome
//! or Edge. The page reaches the card through four commands (its transport "app"): list_cards,
//! open_card, send and close_card. Nothing else is open to the page: no plugins, and in
//! capabilities/default.json only these.

mod card;
mod commands;
mod lines;

use std::sync::Mutex;

use serde::Serialize;
use tauri::ipc::Channel;
use tauri::{State, Url};

/// The card the page has open, at most one.
#[derive(Default)]
struct OpenCard(Mutex<Option<card::Connection>>);

#[derive(Serialize)]
struct CardInfo {
    path: String,
}

#[tauri::command]
fn list_cards() -> Vec<CardInfo> {
    card::list().into_iter().map(|path| CardInfo { path }).collect()
}

/// Opens a card that list_cards gives: never another device. The card's lines and the end of
/// the connection come over `on_event`.
#[tauri::command]
fn open_card(path: String, on_event: Channel<card::Event>, open: State<'_, OpenCard>) -> Result<(), String> {
    let mut slot = open.0.lock().map_err(|_| "failed")?;
    if slot.is_some() {
        return Err("busy".into()); // the page closes a card before it opens one
    }
    if !card::list().contains(&path) {
        return Err("not-found".into());
    }
    let events = move |event| {
        let _ = on_event.send(event); // the window may be closing
    };
    let connection = card::Connection::open(&card::SystemPorts, &path, events).map_err(|error| error.word())?;
    *slot = Some(connection);
    Ok(())
}

#[tauri::command]
fn send(command: String, open: State<'_, OpenCard>) -> Result<(), String> {
    let slot = open.0.lock().map_err(|_| "failed")?;
    let connection = slot.as_ref().ok_or("not-open")?;
    connection.send(&command).map_err(|error| error.word().into())
}

#[tauri::command]
fn close_card(open: State<'_, OpenCard>) {
    let connection = open.0.lock().ok().and_then(|mut slot| slot.take());
    if let Some(connection) = connection {
        connection.close();
    }
}

/// The window shows only the app's own page: tauri://localhost on Linux and macOS,
/// http://tauri.localhost on Windows, and in a development build (`cargo tauri dev`) the page
/// server of the Tauri CLI at `dev_url`. A link to another site does nothing.
fn own_page(url: &Url, dev_url: Option<&Url>) -> bool {
    url.scheme() == "tauri"
        || url.host_str() == Some("tauri.localhost")
        || dev_url.is_some_and(|dev| dev.origin() == url.origin())
}

/// Starts the app: one window with the page from app/dist (tools/app_frontend.py).
pub fn run() {
    tauri::Builder::default()
        .manage(OpenCard::default())
        .invoke_handler(tauri::generate_handler![list_cards, open_card, send, close_card])
        .setup(|app| {
            // The window of tauri.conf.json, made here to add the navigation check.
            let config = app
                .config()
                .app
                .windows
                .first()
                .ok_or("no window in tauri.conf.json")?
                .clone();
            // `cargo tauri dev` serves app/dist itself and sets build.devUrl to its address
            // (http://127.0.0.1:1430/); a release build loads the page from inside the app.
            let dev_url = app.config().build.dev_url.clone().filter(|_| cfg!(dev));
            tauri::WebviewWindowBuilder::from_config(app.handle(), &config)?
                .on_navigation(move |url| own_page(url, dev_url.as_ref()))
                .build()?;
            Ok(())
        })
        .run(tauri::generate_context!())
        .expect("the app could not start");
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn the_window_stays_on_the_apps_own_page() {
        for own in [
            "tauri://localhost/index.html",
            "http://tauri.localhost/",
            "https://tauri.localhost/index.html",
        ] {
            assert!(own_page(&Url::parse(own).unwrap(), None), "{own}");
        }
        for other in [
            "https://github.com/TomaszBojanowski/gpxcam-t1000e",
            "http://127.0.0.1:1430/",
            "file:///etc/passwd",
            "https://tauri.localhost.example.com/",
        ] {
            assert!(!own_page(&Url::parse(other).unwrap(), None), "{other}");
        }
    }

    #[test]
    fn a_development_build_also_shows_the_page_of_the_tauri_cli() {
        let dev = Url::parse("http://127.0.0.1:1430/").unwrap();
        for own in [
            "http://127.0.0.1:1430/",
            "http://127.0.0.1:1430/index.html",
            "tauri://localhost/",
        ] {
            assert!(own_page(&Url::parse(own).unwrap(), Some(&dev)), "{own}");
        }
        for other in [
            "http://127.0.0.1:1431/",
            "http://localhost:1430/",
            "https://127.0.0.1:1430/",
            "https://github.com/TomaszBojanowski/gpxcam-t1000e",
        ] {
            assert!(!own_page(&Url::parse(other).unwrap(), Some(&dev)), "{other}");
        }
    }
}
