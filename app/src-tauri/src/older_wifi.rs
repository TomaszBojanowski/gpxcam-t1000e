//! The one-time Wi-Fi step for pairing an older LUMIX camera (docs/PROTOCOL.md). After the card has
//! introduced itself over Bluetooth, the camera opens its own Wi-Fi access point; the user joins it
//! by hand, and the app registers the same identity and device name the card showed, over the
//! camera's local HTTP service. The app connects only to the camera's own address (192.168.54.1),
//! its one SSDP search leaves only through the camera's network, and nothing goes to the internet.
//! Plain `TcpStream`, no HTTP library.
//!
//! The byte encoding and the request sequence: docs/PROTOCOL.md ("Older dialect: the one-time Wi-Fi
//! step"). The User-Agent here is a neutral one, not the older app's name; the S5II accepts it.

use std::io::{self, Read, Write};
use std::net::{IpAddr, Ipv4Addr, SocketAddr, TcpStream, UdpSocket};
use std::sync::atomic::{AtomicBool, Ordering};
use std::time::{Duration, Instant};

use serde::Serialize;
use socket2::{Domain, Protocol, Socket, Type};

use crate::Problem;

/// The camera's address on its own access point, and the UPnP port and SSDP address used only to
/// warm the camera up.
pub struct Target {
    pub camera: SocketAddr,
    pub upnp_port: u16,
    pub ssdp: SocketAddr,
}

/// The camera on its access point: always 192.168.54.1, HTTP on 80, UPnP on 60606; the SSDP search
/// goes to the standard SSDP multicast group.
pub fn camera_target() -> Target {
    Target {
        camera: SocketAddr::new(IpAddr::V4(Ipv4Addr::new(192, 168, 54, 1)), 80),
        upnp_port: 60606,
        ssdp: SocketAddr::new(IpAddr::V4(Ipv4Addr::new(239, 255, 255, 250)), 1900),
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
    // The warm-up before the registration: a UPnP read, `after_read`, the SSDP search (its socket
    // open for `search_open`), `before_second_read`, a second UPnP read, `after_read`.
    pub upnp_timeout: Duration, // connect, write and read of each UPnP read
    pub after_read: Duration,
    pub search_open: Duration,
    pub before_second_read: Duration,
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
            // the gaps the older app used: the two UPnP reads about 2 s apart
            upnp_timeout: Duration::from_secs(2),
            after_read: Duration::from_millis(100),
            search_open: Duration::from_millis(200),
            before_second_read: Duration::from_millis(1900),
        }
    }
}

/// What the page is told while the step runs (the final answer is the command's `Outcome`).
#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize)]
#[serde(tag = "kind", rename_all = "kebab-case")]
pub enum Progress {
    OnNetwork,   // reached the camera
    Registering, // sent the registration; the camera (and the user) must accept
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
pub fn check_identity(identity: &str) -> Result<(), Problem> {
    const PREFIX: &str = "4D454930-0100-1000-8001-";
    if identity.len() == 36
        && identity.starts_with(PREFIX)
        && identity[PREFIX.len()..].bytes().all(|b| b.is_ascii_hexdigit())
    {
        Ok(())
    } else {
        Err(Problem::BadIdentity)
    }
}

/// The device name the card shows: printable ASCII, 1 to 16 characters (device_identity name field).
pub fn check_name(name: &str) -> Result<(), Problem> {
    if (1..=16).contains(&name.len()) && name.bytes().all(|b| b.is_ascii_graphic()) {
        Ok(())
    } else {
        Err(Problem::BadName)
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

/// The Host header: the bare IP for port 80 (as the camera uses), else "ip:port".
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
    let reply = http_get(&mut stream, &host, "/cam.cgi?mode=accctrl&type=req_acc_g")?;
    let Some(token) = parse_token(&reply) else {
        eprintln!("older camera Wi-Fi step: req_acc_g answered {reply:?}");
        return Ok(Outcome::Failed);
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
        if reply.starts_with("ok") {
            return Ok(Outcome::Accepted);
        }
        eprintln!("older camera Wi-Fi step: req_acc_e answered {reply:?}");
        return Ok(Outcome::Failed);
    }
    Ok(Outcome::NotConfirmed) // the camera kept saying "wait" for every attempt
}

/// Waits until a connection to the camera succeeds (the user has joined its network) or the time
/// runs out, and returns that connection.
fn wait_for_network(target: &Target, timing: &Timing, cancel: &AtomicBool) -> Option<TcpStream> {
    let deadline = Instant::now() + timing.network_wait;
    loop {
        if cancel.load(Ordering::SeqCst) {
            return None;
        }
        if let Ok(stream) = TcpStream::connect_timeout(&target.camera, timing.connect_timeout) {
            return Some(stream);
        }
        if Instant::now() >= deadline {
            return None;
        }
        std::thread::sleep(timing.network_poll);
    }
}

/// This computer's address on the camera's network: the local end of a connection that reached the
/// camera, if it is an IPv4 address in the camera's /24 (192.168.54.x). Anything else means the
/// camera was reached some other way, and the SSDP search is not sent.
fn camera_network_address(local: IpAddr, camera: IpAddr) -> Option<Ipv4Addr> {
    match (local, camera) {
        (IpAddr::V4(local), IpAddr::V4(camera)) if local.octets()[..3] == camera.octets()[..3] => Some(local),
        _ => None,
    }
}

/// Best-effort replay of the UPnP description read and an SSDP search, with the older app's gaps
/// (`Timing`), before the registration. Every error is ignored; it only nudges the camera. The
/// search goes out only through `local`, this computer's address on the camera's network.
fn warm_up(target: &Target, local: Option<Ipv4Addr>, timing: &Timing) {
    let upnp = SocketAddr::new(target.camera.ip(), target.upnp_port);
    let ddd = || {
        if let Ok(mut stream) = TcpStream::connect_timeout(&upnp, timing.upnp_timeout) {
            let _ = stream.set_read_timeout(Some(timing.upnp_timeout));
            let _ = stream.set_write_timeout(Some(timing.upnp_timeout));
            let _ = http_get(&mut stream, &host_header(&upnp), UPNP_PATH);
        }
        std::thread::sleep(timing.after_read);
    };
    ddd();
    if let Some(local) = local {
        let _ = ssdp_search(local, target.ssdp, timing.search_open);
    }
    std::thread::sleep(timing.before_second_read);
    ddd();
}

const SSDP_SEARCH: &str = "M-SEARCH * HTTP/1.1\r\nHOST: 239.255.255.250:1900\r\nMAN: \"ssdp:discover\"\r\nMX: 2\r\n\
                           ST: urn:schemas-upnp-org:device:MediaServer:1\r\n\r\n";
const SSDP_TTL: u32 = 2;

/// A socket that sends only from `local`. Without IP_MULTICAST_IF the system picks the interface for
/// a multicast from its routing table, which may be another network; IP_MULTICAST_IF set to `local`
/// sends it out through the camera's network (ip(7) on Linux, ip(4) on macOS, IPPROTO_IP socket
/// options in Winsock). The bind makes `local` the packet's source as well.
fn ssdp_socket(local: Ipv4Addr) -> io::Result<UdpSocket> {
    let socket = Socket::new(Domain::IPV4, Type::DGRAM, Some(Protocol::UDP))?;
    socket.set_multicast_if_v4(&local)?;
    socket.set_multicast_ttl_v4(SSDP_TTL)?;
    socket.bind(&SocketAddr::new(IpAddr::V4(local), 0).into())?;
    Ok(socket.into())
}

/// One SSDP search from `local`. The socket stays open for `open`; the camera's answer is not read.
fn ssdp_search(local: Ipv4Addr, destination: SocketAddr, open: Duration) -> io::Result<()> {
    let socket = ssdp_socket(local)?;
    socket.send_to(SSDP_SEARCH.as_bytes(), destination)?;
    std::thread::sleep(open);
    Ok(())
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
    let Some(reached) = wait_for_network(target, timing, cancel) else {
        return if cancel.load(Ordering::SeqCst) {
            Outcome::Cancelled
        } else {
            Outcome::NotOnNetwork
        };
    };
    let local = reached
        .local_addr()
        .ok()
        .and_then(|address| camera_network_address(address.ip(), target.camera.ip()));
    drop(reached);
    progress(Progress::OnNetwork);
    warm_up(target, local, timing);
    progress(Progress::Registering);
    request_access(target, identity, name, timing, cancel).unwrap_or_else(|error| {
        eprintln!("older camera Wi-Fi step: {error}");
        Outcome::Failed
    })
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::collections::{HashMap, VecDeque};
    use std::net::TcpListener;
    use std::sync::{Arc, Mutex};
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
            upnp_timeout: Duration::from_millis(200),
            after_read: Duration::ZERO,
            search_open: Duration::ZERO,
            before_second_read: Duration::ZERO,
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

    // The camera's HTTP and UPnP on one port, and `ssdp` for the search.
    fn target_at(camera: SocketAddr, ssdp: &UdpSocket) -> Target {
        Target {
            camera,
            upnp_port: camera.port(),
            ssdp: ssdp.local_addr().unwrap(),
        }
    }

    // A camera on 127.0.0.1 that answers req_acc_g with `g_reply`, req_acc_e with the scripted
    // replies in order (the last one repeats) and the UPnP read with "ok". It keeps the first line of
    // each request in `requests` and the SSDP search in `ssdp`; it ends when the process does.
    struct FakeCamera {
        target: Target,
        requests: Arc<Mutex<Vec<String>>>,
        ssdp: UdpSocket,
    }

    fn fake_camera(g_reply: &'static str, acc_e: &[&'static str]) -> FakeCamera {
        let listener = TcpListener::bind("127.0.0.1:0").unwrap();
        let ssdp = UdpSocket::bind("127.0.0.1:0").unwrap();
        let target = target_at(listener.local_addr().unwrap(), &ssdp);
        let requests = Arc::new(Mutex::new(Vec::new()));
        let seen = Arc::clone(&requests);
        let mut replies: VecDeque<String> = acc_e.iter().map(|reply| reply.to_string()).collect();
        thread::spawn(move || {
            for stream in listener.incoming() {
                let Ok(mut stream) = stream else { break };
                let _ = stream.set_read_timeout(Some(Duration::from_secs(2)));
                while let Some(request) = read_request(&mut stream) {
                    seen.lock()
                        .unwrap()
                        .push(request.lines().next().unwrap_or_default().to_string());
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
        FakeCamera { target, requests, ssdp }
    }

    // A camera at a closed port: connecting is refused, so the step never reaches the UPnP port or
    // the SSDP address.
    fn unreachable_camera() -> Target {
        let camera = TcpListener::bind("127.0.0.1:0").unwrap().local_addr().unwrap();
        Target {
            camera,
            upnp_port: camera.port(),
            ssdp: camera,
        }
    }

    fn run(target: &Target, cancelled: bool) -> (Outcome, Vec<Progress>) {
        let vectors = vectors();
        let cancel = AtomicBool::new(cancelled);
        let mut events = Vec::new();
        let outcome = register(
            target,
            &vectors["identity_text"],
            &vectors["name"],
            &test_timing(),
            &cancel,
            &mut |event| events.push(event),
        );
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
        assert_eq!(check_identity("4D454930-0100-1000-8001-A1B2C3D4E5F6"), Ok(()));
        for wrong in [
            "4D454930-0100-1000-8001-A1B2C3D4E5",   // too short
            "00000000-0100-1000-8001-A1B2C3D4E5F6", // wrong prefix
            "4D454930-0100-1000-8001-A1B2C3D4E5FZ", // non-hex seed
        ] {
            assert_eq!(check_identity(wrong), Err(Problem::BadIdentity), "{wrong}");
        }
        assert_eq!(check_name("T1000-E"), Ok(()));
        assert_eq!(check_name(""), Err(Problem::BadName));
        assert_eq!(check_name("a name with spaces"), Err(Problem::BadName));
    }

    #[test]
    fn accepts_once_the_camera_says_ok() {
        let camera = fake_camera("ok,deadbeef", &["ok_under_research,x", "ok,CAM,remote,open,abcd"]);
        let (outcome, events) = run(&camera.target, false);
        assert_eq!(outcome, Outcome::Accepted);
        assert_eq!(events, [Progress::OnNetwork, Progress::Registering]);
    }

    #[test]
    fn accepts_after_the_user_was_asked_on_the_camera() {
        let camera = fake_camera("ok,deadbeef", &["err_user_refused,x", "ok,CAM,remote,open,abcd"]);
        assert_eq!(run(&camera.target, false).0, Outcome::Accepted);
    }

    #[test]
    fn not_confirmed_when_the_camera_never_decides() {
        let camera = fake_camera("ok,deadbeef", &["ok_under_research,x"]);
        assert_eq!(run(&camera.target, false).0, Outcome::NotConfirmed);
    }

    #[test]
    fn fails_on_an_unexpected_reply() {
        let camera = fake_camera("ok,deadbeef", &["err_something_else"]);
        assert_eq!(run(&camera.target, false).0, Outcome::Failed);
    }

    #[test]
    fn fails_when_req_acc_g_has_no_token() {
        let camera = fake_camera("err", &["ok,CAM"]);
        assert_eq!(run(&camera.target, false).0, Outcome::Failed);
    }

    #[test]
    fn warms_up_with_two_upnp_reads_and_one_ssdp_search_before_registering() {
        let camera = fake_camera("ok,deadbeef", &["ok,CAM,remote,open,abcd"]);
        assert_eq!(run(&camera.target, false).0, Outcome::Accepted);

        let requests = camera.requests.lock().unwrap();
        assert_eq!(
            requests[..3],
            [
                "GET /Lumix/Server0/ddd HTTP/1.1",
                "GET /Lumix/Server0/ddd HTTP/1.1",
                "GET /cam.cgi?mode=accctrl&type=req_acc_g HTTP/1.1",
            ]
        );
        assert_eq!(requests.len(), 4, "then req_acc_e: {requests:?}");

        let mut buffer = [0u8; 512];
        camera.ssdp.set_read_timeout(Some(Duration::from_millis(200))).unwrap();
        let (length, from) = camera.ssdp.recv_from(&mut buffer).unwrap();
        assert_eq!(&buffer[..length], SSDP_SEARCH.as_bytes());
        assert_eq!(from.ip(), IpAddr::V4(Ipv4Addr::LOCALHOST)); // the local end of the connection to the camera
        assert!(camera.ssdp.recv_from(&mut buffer).is_err(), "only one search");
    }

    #[test]
    fn the_ssdp_search_is_sent_only_from_an_address_on_the_cameras_network() {
        let camera = IpAddr::V4(Ipv4Addr::new(192, 168, 54, 1));
        let on_camera_network = Ipv4Addr::new(192, 168, 54, 10);
        assert_eq!(
            camera_network_address(IpAddr::V4(on_camera_network), camera),
            Some(on_camera_network)
        );
        assert_eq!(camera_network_address("192.168.1.10".parse().unwrap(), camera), None);
        assert_eq!(camera_network_address("10.0.0.2".parse().unwrap(), camera), None);
        assert_eq!(camera_network_address("fe80::1".parse().unwrap(), camera), None);
    }

    #[test]
    fn the_ssdp_socket_sends_multicast_only_through_the_given_address() {
        let local = Ipv4Addr::LOCALHOST;
        let socket = ssdp_socket(local).unwrap();
        let options = socket2::SockRef::from(&socket);
        assert_eq!(options.multicast_if_v4().unwrap(), local);
        assert_eq!(options.multicast_ttl_v4().unwrap(), SSDP_TTL);
        assert_eq!(socket.local_addr().unwrap().ip(), IpAddr::V4(local));
    }

    #[test]
    fn not_on_network_when_the_camera_cannot_be_reached() {
        let (outcome, events) = run(&unreachable_camera(), false);
        assert_eq!(outcome, Outcome::NotOnNetwork);
        assert_eq!(events, []);
    }

    #[test]
    fn cancelled_stops_the_step() {
        // already cancelled: it stops before the network
        assert_eq!(run(&unreachable_camera(), true).0, Outcome::Cancelled);
    }

    #[test]
    fn fails_when_the_camera_does_not_answer() {
        let listener = TcpListener::bind("127.0.0.1:0").unwrap();
        let ssdp = UdpSocket::bind("127.0.0.1:0").unwrap();
        let target = target_at(listener.local_addr().unwrap(), &ssdp);
        thread::spawn(move || {
            for stream in listener.incoming() {
                let Ok(mut stream) = stream else { break };
                let _ = stream.set_read_timeout(Some(Duration::from_secs(2)));
                let _ = read_request(&mut stream); // read, then never answer
                thread::sleep(Duration::from_millis(300));
            }
        });
        assert_eq!(run(&target, false).0, Outcome::Failed);
    }
}
