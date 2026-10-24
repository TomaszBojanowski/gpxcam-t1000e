//! gpxcam card: the card page (docs/index.html) in its own window, for computers without Chrome
//! or Edge. The page reaches the card through this side (its transport "app").

/// Starts the app: one window with the page from app/dist (tools/app_frontend.py).
pub fn run() {
    tauri::Builder::default()
        .run(tauri::generate_context!())
        .expect("the app could not start");
}
