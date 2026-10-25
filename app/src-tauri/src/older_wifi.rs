//! The one-time Wi-Fi step for pairing an older LUMIX camera (docs/PROTOCOL.md). After the card has
//! introduced itself over Bluetooth, the camera opens its own Wi-Fi access point; the user joins it
//! by hand, and the app registers the same identity and device name the card showed, over the
//! camera's local HTTP service. The app reaches only the camera's own address (192.168.54.1) and
//! never the internet. Plain `TcpStream`, no HTTP library.
//!
//! The byte encoding and the request sequence come from local notes of the older protocol
//! (local/plan-older-cameras.md); the User-Agent here is a neutral one, not the older app's name -
//! whether the camera checks it is a test question.

use std::io::{self, Read, Write};
use std::net::{IpAddr, Ipv4Addr, SocketAddr, TcpStream, UdpSocket};
use std::sync::atomic::{AtomicBool, Ordering};
use std::time::{Duration, Instant};

use serde::Serialize;

/// The camera's address on its own access point, and the UPnP port used only to warm the camera up.
pub struct Target {
    pub camera: SocketAddr,
    pub upnp_port: u16,
}

/// The camera on its access point: always 192.168.54.1, HTTP on 80, UPnP on 60606.
pub fn camera_target() -> Target {
    Target {
        camera: SocketAddr::new(IpAddr::V4(Ipv4Addr::new(192, 168, 54, 1)), 80),
        upnp_port: 60606,
    }
}

/// How long each stage may take. `live()` is what the app uses; the tests use short values.
pub struct Timing {
    pub network_wait: Duration, // total time to wait for the user to join the camera's network
    pub network_poll: Duration, // between "are we on the network yet?" checks
    pub connect_timeout: Duration,
    pub io_timeout: Duration,
    pub tries: u32,    // req_acc_e attempts while the camera has not decided
    pub gap: Duration, // between those attempts
    pub warmup: bool,  // replay the UPnP/SSDP warm-up (off in the tests)
}

impl Timing {
    pub fn live() -> Timing {
        Timing {
            network_wait: Duration::from_secs(300), // the card waits as long for the camera's 01
            network_poll: Duration::from_secs(1),
            connect_timeout: Duration::from_secs(3),
            io_timeout: Duration::from_secs(6),
            tries: 25,
            gap: Duration::from_millis(500),
            warmup: true,
        }
    }
}

/// What the page is told while the step runs (the final answer is the command's `Outcome`).
#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize)]
#[serde(tag = "kind", rename_all = "kebab-case")]
pub enum Progress {
    WaitingForNetwork, // waiting for the user to join the camera's Wi-Fi
    OnNetwork,         // reached the camera
    Registering,       // sent the registration; the camera (and the user) must accept
}

/// How the step ended.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize)]
#[serde(rename_all = "kebab-case")]
pub enum Outcome {
    Accepted,     // the camera took the card
    NotConfirmed, // reached the camera, but it never accepted (the user did not confirm in time)
    NotOnNetwork, // never reached the camera's Wi-Fi
    Failed,       // an unexpected reply or a connection problem
    Cancelled,    // the user stopped the step
}

const USER_AGENT: &str = "gpxcam-card";
const UPNP_PATH: &str = "/Lumix/Server0/ddd";

/// The identity text the card shows (device_identity::identityText): the 10-byte prefix and the
/// 6-byte seed as 36 upper-case characters. The app registers exactly this.
pub fn check_identity(identity: &str) -> Result<(), String> {
    const PREFIX: &str = "4D454930-0100-1000-8001-";
    if identity.len() == 36
        && identity.starts_with(PREFIX)
        && identity[PREFIX.len()..].bytes().all(|b| b.is_ascii_hexdigit())
    {
        Ok(())
    } else {
        Err("the identity is not the 4D454930-0100-1000-8001-<seed> text".into())
    }
}

/// The device name the card shows: printable ASCII, 1 to 16 characters (device_identity name field).
pub fn check_name(name: &str) -> Result<(), String> {
    if (1..=16).contains(&name.len()) && name.bytes().all(|b| b.is_ascii_graphic()) {
        Ok(())
    } else {
        Err("the device name is not 1 to 16 printable ASCII characters".into())
    }
}

/// The Wi-Fi value encoding: pad the ASCII text with 0xFF to a multiple of four bytes, then for each
/// four-byte word XOR it with the session token and emit the byte-swapped result as eight lower-case
/// hex digits. The same XOR-and-byte-swap as the card's Bluetooth identity (device_identity), here
/// over the text form, with the token the camera gives in reply to req_acc_g.
fn encode(text: &str, token: u32) -> String {
    let mut data = text.as_bytes().to_vec();
    while data.len() % 4 != 0 {
        data.push(0xFF);
    }
    let mut out = String::with_capacity(data.len() * 2);
    for chunk in data.chunks_exact(4) {
        let word = u32::from_be_bytes([chunk[0], chunk[1], chunk[2], chunk[3]]);
        out.push_str(&format!("{:08x}", (word ^ token).swap_bytes()));
    }
    out
}

/// The Host header: the bare IP for port 80 (as the camera uses), else "ip:port" (the tests).
fn host_header(addr: &SocketAddr) -> String {
    if addr.port() == 80 {
        addr.ip().to_string()
    } else {
        addr.to_string()
    }
}

fn find(haystack: &[u8], needle: &[u8]) -> Option<usize> {
    haystack.windows(needle.len()).position(|window| window == needle)
}

fn content_length(headers: &[u8]) -> Option<usize> {
    for line in String::from_utf8_lossy(headers).split("\r\n") {
        if let Some((name, value)) = line.split_once(':') {
            if name.trim().eq_ignore_ascii_case("content-length") {
                return value.trim().parse().ok();
            }
        }
    }
    None
}

/// One HTTP/1.1 GET on an open connection, returning the body as trimmed text. No gzip is asked for,
/// so the camera's short replies come as plain text.
fn http_get(stream: &mut TcpStream, host: &str, path: &str) -> io::Result<String> {
    let request =
        format!("GET {path} HTTP/1.1\r\nUser-Agent: {USER_AGENT}\r\nHost: {host}\r\nConnection: Keep-Alive\r\n\r\n");
    stream.write_all(request.as_bytes())?;
    stream.flush()?;
    let mut buffer = Vec::new();
    let mut chunk = [0u8; 512];
    let header_end = loop {
        if let Some(position) = find(&buffer, b"\r\n\r\n") {
            break position;
        }
        let read = stream.read(&mut chunk)?;
        if read == 0 {
            return Err(io::ErrorKind::UnexpectedEof.into());
        }
        buffer.extend_from_slice(&chunk[..read]);
    };
    let length = content_length(&buffer[..header_end]);
    let mut body = buffer[header_end + 4..].to_vec();
    if let Some(length) = length {
        while body.len() < length {
            let read = stream.read(&mut chunk)?;
            if read == 0 {
                break;
            }
            body.extend_from_slice(&chunk[..read]);
        }
        body.truncate(length);
    }
    Ok(String::from_utf8_lossy(&body).trim().to_string())
}

/// "ok,<hex>" from req_acc_g gives the four-byte session token.
fn parse_token(reply: &str) -> Option<u32> {
    let mut parts = reply.split(',');
    if parts.next()? != "ok" {
        return None;
    }
    u32::from_str_radix(parts.next()?.trim(), 16).ok()
}

/// req_acc_g then req_acc_e until the camera decides. A reply of "ok_under_research" or
/// "err_user_refused" means it is still waiting for the user; any other reply is final.
fn request_access(
    target: &Target,
    identity: &str,
    name: &str,
    timing: &Timing,
    cancel: &AtomicBool,
) -> io::Result<Outcome> {
    let mut stream = TcpStream::connect_timeout(&target.camera, timing.connect_timeout)?;
    stream.set_read_timeout(Some(timing.io_timeout))?;
    stream.set_write_timeout(Some(timing.io_timeout))?;
    let host = host_header(&target.camera);
    let token = match parse_token(&http_get(&mut stream, &host, "/cam.cgi?mode=accctrl&type=req_acc_g")?) {
        Some(token) => token,
        None => return Ok(Outcome::Failed),
    };
    let path = format!(
        "/cam.cgi?mode=accctrl&type=req_acc_e&value={}&value2={}",
        encode(identity, token),
        encode(name, token)
    );
    for attempt in 0..timing.tries {
        if cancel.load(Ordering::SeqCst) {
            return Ok(Outcome::Cancelled);
        }
        let reply = http_get(&mut stream, &host, &path)?;
        if reply.starts_with("ok_under_research") || reply.starts_with("err_user_refused") {
            if attempt + 1 < timing.tries {
                std::thread::sleep(timing.gap);
            }
            continue;
        }
        return Ok(if reply.starts_with("ok") {
            Outcome::Accepted
        } else {
            Outcome::Failed
        });
    }
    Ok(Outcome::NotConfirmed) // the camera kept saying "wait" for every attempt
}

/// Waits until a connection to the camera succeeds (the user has joined its network) or the time
/// runs out.
fn wait_for_network(target: &Target, timing: &Timing, cancel: &AtomicBool) -> bool {
    let deadline = Instant::now() + timing.network_wait;
    loop {
        if cancel.load(Ordering::SeqCst) {
            return false;
        }
        if TcpStream::connect_timeout(&target.camera, timing.connect_timeout).is_ok() {
            return true;
        }
        if Instant::now() >= deadline {
            return false;
        }
        std::thread::sleep(timing.network_poll);
    }
}

/// Best-effort replay of the UPnP description read and an SSDP search, with the gaps the older app
/// used, before the registration. Every error is ignored; it only nudges the camera.
fn warm_up(target: &Target) {
    let upnp = SocketAddr::new(target.camera.ip(), target.upnp_port);
    let ddd = || {
        if let Ok(mut stream) = TcpStream::connect_timeout(&upnp, Duration::from_secs(2)) {
            let _ = stream.set_read_timeout(Some(Duration::from_secs(2)));
            let _ = stream.set_write_timeout(Some(Duration::from_secs(2)));
            let _ = http_get(&mut stream, &host_header(&upnp), UPNP_PATH);
        }
    };
    ddd();
    std::thread::sleep(Duration::from_millis(100));
    ssdp_search();
    std::thread::sleep(Duration::from_millis(1900));
    ddd();
    std::thread::sleep(Duration::from_millis(100));
}

fn ssdp_search() {
    if let Ok(socket) = UdpSocket::bind("0.0.0.0:0") {
        let _ = socket.set_multicast_ttl_v4(2);
        let message = "M-SEARCH * HTTP/1.1\r\nHOST: 239.255.255.250:1900\r\nMAN: \"ssdp:discover\"\r\nMX: 2\r\n\
                       ST: urn:schemas-upnp-org:device:MediaServer:1\r\n\r\n";
        let _ = socket.send_to(message.as_bytes(), "239.255.255.250:1900");
        std::thread::sleep(Duration::from_millis(200));
    }
}

/// The whole step: wait for the camera's network, warm it up, register, and report the stages
/// through `progress`. Assumes `identity` and `name` were already checked.
pub fn register(
    target: &Target,
    identity: &str,
    name: &str,
    timing: &Timing,
    cancel: &AtomicBool,
    progress: &mut dyn FnMut(Progress),
) -> Outcome {
    progress(Progress::WaitingForNetwork);
    if !wait_for_network(target, timing, cancel) {
        return if cancel.load(Ordering::SeqCst) {
            Outcome::Cancelled
        } else {
            Outcome::NotOnNetwork
        };
    }
    progress(Progress::OnNetwork);
    if timing.warmup {
        warm_up(target);
    }
    progress(Progress::Registering);
    request_access(target, identity, name, timing, cancel).unwrap_or(Outcome::Failed)
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::collections::{HashMap, VecDeque};
    use std::net::TcpListener;
    use std::thread;

    // The made-up identity and name of test/device_identity_vectors.txt, shared with the card's
    // C++ test, so the app encodes exactly the identity the card shows.
    const VECTORS: &str = include_str!("../../../test/device_identity_vectors.txt");

    fn vectors() -> HashMap<String, String> {
        VECTORS
            .lines()
            .filter(|line| !line.trim_start().starts_with('#'))
            .filter_map(|line| line.split_once('='))
            .map(|(name, value)| (name.trim().to_string(), value.trim().to_string()))
            .collect()
    }

    fn test_timing() -> Timing {
        Timing {
            network_wait: Duration::from_millis(500),
            network_poll: Duration::from_millis(20),
            connect_timeout: Duration::from_millis(300),
            io_timeout: Duration::from_millis(500),
            tries: 4,
            gap: Duration::from_millis(5),
            warmup: false,
        }
    }

    fn read_request(stream: &mut TcpStream) -> Option<String> {
        let mut buffer = Vec::new();
        let mut chunk = [0u8; 256];
        loop {
            if let Some(position) = find(&buffer, b"\r\n\r\n") {
                return Some(String::from_utf8_lossy(&buffer[..position]).to_string());
            }
            match stream.read(&mut chunk) {
                Ok(0) | Err(_) => return None,
                Ok(read) => buffer.extend_from_slice(&chunk[..read]),
            }
        }
    }

    fn respond(stream: &mut TcpStream, body: &str) {
        let message = format!(
            "HTTP/1.1 200 OK\r\nContent-Length: {}\r\nConnection: keep-alive\r\n\r\n{body}",
            body.len()
        );
        let _ = stream.write_all(message.as_bytes());
        let _ = stream.flush();
    }

    // A camera that answers req_acc_g with `g_reply` and req_acc_e with the scripted replies in
    // order (the last one repeats). Runs on 127.0.0.1 and ends when the process does.
    fn fake_camera(g_reply: &'static str, acc_e: &[&'static str]) -> SocketAddr {
        let listener = TcpListener::bind("127.0.0.1:0").unwrap();
        let address = listener.local_addr().unwrap();
        let mut replies: VecDeque<String> = acc_e.iter().map(|reply| reply.to_string()).collect();
        thread::spawn(move || {
            for stream in listener.incoming() {
                let Ok(mut stream) = stream else { break };
                let _ = stream.set_read_timeout(Some(Duration::from_secs(2)));
                while let Some(request) = read_request(&mut stream) {
                    if request.contains("type=req_acc_g") {
                        respond(&mut stream, g_reply);
                    } else if request.contains("type=req_acc_e") {
                        let reply = if replies.len() > 1 {
                            replies.pop_front().unwrap()
                        } else {
                            replies.front().cloned().unwrap_or_default()
                        };
                        respond(&mut stream, &reply);
                    } else {
                        respond(&mut stream, "ok");
                    }
                }
            }
        });
        address
    }

    fn run(camera: SocketAddr) -> (Outcome, Vec<Progress>) {
        let identity = vectors()["identity_text"].clone();
        let name = vectors()["name"].clone();
        let target = Target { camera, upnp_port: 0 };
        let cancel = AtomicBool::new(false);
        let mut events = Vec::new();
        let outcome = register(&target, &identity, &name, &test_timing(), &cancel, &mut |event| {
            events.push(event)
        });
        (outcome, events)
    }

    #[test]
    fn encode_matches_the_shared_identity_and_name_vectors() {
        let vectors = vectors();
        let token = 0x1a2b_3c4d;
        assert_eq!(
            encode(&vectors["identity_text"], token),
            "78086f2e7d0f122e7d0d1b377d0d062a75111b2a600d1b2a7f7e1a5b797818597b7a1e5f"
        );
        assert_eq!(encode(&vectors["name"], token), "7d0c1a4eb279062a");
    }

    #[test]
    fn checks_the_identity_and_the_name() {
        assert!(check_identity("4D454930-0100-1000-8001-A1B2C3D4E5F6").is_ok());
        assert!(check_identity("4D454930-0100-1000-8001-A1B2C3D4E5").is_err()); // too short
        assert!(check_identity("00000000-0100-1000-8001-A1B2C3D4E5F6").is_err()); // wrong prefix
        assert!(check_identity("4D454930-0100-1000-8001-A1B2C3D4E5FZ").is_err()); // non-hex seed
        assert!(check_name("T1000-E").is_ok());
        assert!(check_name("").is_err());
        assert!(check_name("a name with spaces").is_err());
    }

    #[test]
    fn accepts_once_the_camera_says_ok() {
        let camera = fake_camera("ok,deadbeef", &["ok_under_research,x", "ok,CAM,remote,open,abcd"]);
        let (outcome, events) = run(camera);
        assert_eq!(outcome, Outcome::Accepted);
        assert_eq!(
            events,
            [Progress::WaitingForNetwork, Progress::OnNetwork, Progress::Registering]
        );
    }

    #[test]
    fn accepts_after_the_user_was_asked_on_the_camera() {
        let camera = fake_camera("ok,deadbeef", &["err_user_refused,x", "ok,CAM,remote,open,abcd"]);
        assert_eq!(run(camera).0, Outcome::Accepted);
    }

    #[test]
    fn not_confirmed_when_the_camera_never_decides() {
        let camera = fake_camera("ok,deadbeef", &["ok_under_research,x"]);
        assert_eq!(run(camera).0, Outcome::NotConfirmed);
    }

    #[test]
    fn fails_on_an_unexpected_reply() {
        let camera = fake_camera("ok,deadbeef", &["err_something_else"]);
        assert_eq!(run(camera).0, Outcome::Failed);
    }

    #[test]
    fn fails_when_req_acc_g_has_no_token() {
        let camera = fake_camera("err", &["ok,CAM"]);
        assert_eq!(run(camera).0, Outcome::Failed);
    }

    #[test]
    fn not_on_network_when_the_camera_cannot_be_reached() {
        let listener = TcpListener::bind("127.0.0.1:0").unwrap();
        let address = listener.local_addr().unwrap();
        drop(listener); // a closed port: connecting is refused
        let (outcome, events) = run(address);
        assert_eq!(outcome, Outcome::NotOnNetwork);
        assert_eq!(events, [Progress::WaitingForNetwork]);
    }

    #[test]
    fn cancelled_stops_the_step() {
        let listener = TcpListener::bind("127.0.0.1:0").unwrap();
        let address = listener.local_addr().unwrap();
        drop(listener);
        let cancel = AtomicBool::new(true); // already cancelled: it stops before the network
        let target = Target {
            camera: address,
            upnp_port: 0,
        };
        let vectors = vectors();
        let outcome = register(
            &target,
            &vectors["identity_text"],
            &vectors["name"],
            &test_timing(),
            &cancel,
            &mut |_| {},
        );
        assert_eq!(outcome, Outcome::Cancelled);
    }

    #[test]
    fn fails_when_the_camera_does_not_answer() {
        let listener = TcpListener::bind("127.0.0.1:0").unwrap();
        let address = listener.local_addr().unwrap();
        thread::spawn(move || {
            for stream in listener.incoming() {
                let Ok(mut stream) = stream else { break };
                let _ = stream.set_read_timeout(Some(Duration::from_secs(2)));
                let _ = read_request(&mut stream); // read, then never answer
                thread::sleep(Duration::from_millis(300));
            }
        });
        assert_eq!(run(address).0, Outcome::Failed);
    }
}
