//! The card's serial port: finding the card, opening its port as the page needs it (115200 baud,
//! DTR on), passing its lines to the page from a thread, and sending it the allowed commands.

use std::io::{self, Read, Write};
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Arc, Mutex};
use std::thread::JoinHandle;
use std::time::Duration;

use serde::Serialize;

use crate::commands;
use crate::lines::LineAssembler;

/// The only baud rate the app knows. Any rate works over USB, but 1200 would reset the card into
/// its bootloader (docs/index.html BAUD_RATE).
pub const BAUD_RATE: u32 = 115_200;

/// Seeed, T1000-E running gpxcam (boards/t1000-e.json).
pub const USB_VENDOR_ID: u16 = 0x2886;
pub const USB_PRODUCT_ID: u16 = 0x8057;

/// How long a read or a write waits for the port; also how soon the reading thread notices
/// close(). A command is under 50 bytes, and the card reads its console at once.
const PORT_TIMEOUT: Duration = Duration::from_millis(100);

/// What the page gets over its channel (docs/index.html AppPort): the card's lines, then once
/// Closed, with an error when the port failed (the card was unplugged), without one after close().
#[derive(Clone, Debug, PartialEq, Serialize)]
#[serde(tag = "kind", rename_all = "lowercase")]
pub enum Event {
    Line { text: String },
    Closed { error: Option<String> },
}

/// Why a port did not open: the word the page explains (docs/index.html openProblemText()).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum OpenError {
    Permission,
    Busy,
    NotFound,
    Failed,
}

impl OpenError {
    pub fn word(self) -> &'static str {
        match self {
            OpenError::Permission => "permission",
            OpenError::Busy => "busy",
            OpenError::NotFound => "not-found",
            OpenError::Failed => "failed",
        }
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum SendError {
    Refused, // not a command the page may send: nothing was written
    Failed,  // the port did not take it
}

impl SendError {
    pub fn word(self) -> &'static str {
        match self {
            SendError::Refused => "refused",
            SendError::Failed => "failed",
        }
    }
}

/// A port as the app uses it: the card's (SystemPort, opened by SystemPorts) or a stand-in in the tests.
pub trait CardPort: Read + Write + Send {
    fn set_dtr(&mut self, on: bool) -> io::Result<()>;
    /// Another handle to the same port, for the reading thread.
    fn try_clone_port(&self) -> io::Result<Box<dyn CardPort>>;
}

/// Opens a port: SystemPorts for the card, a stand-in in the tests.
pub trait Opener {
    fn open(&self, path: &str, baud_rate: u32) -> Result<Box<dyn CardPort>, OpenError>;
}

/// The computer's serial ports, through the serialport crate. On Linux and macOS it opens a port
/// exclusively (TIOCEXCL and flock: a serial monitor or the card page then cannot open it too),
/// and raw (cfmakeraw: VMIN=1, VTIME=0, which the port keeps after the app; serialport 4.10
/// src/posix/tty.rs).
pub struct SystemPorts;

impl Opener for SystemPorts {
    fn open(&self, path: &str, baud_rate: u32) -> Result<Box<dyn CardPort>, OpenError> {
        let port = serialport::new(path, baud_rate)
            .timeout(PORT_TIMEOUT)
            .open()
            .map_err(open_error)?;
        Ok(Box::new(SystemPort(port)))
    }
}

struct SystemPort(Box<dyn serialport::SerialPort>);

impl Read for SystemPort {
    fn read(&mut self, buffer: &mut [u8]) -> io::Result<usize> {
        self.0.read(buffer)
    }
}

impl Write for SystemPort {
    fn write(&mut self, bytes: &[u8]) -> io::Result<usize> {
        self.0.write(bytes)
    }
    fn flush(&mut self) -> io::Result<()> {
        self.0.flush()
    }
}

impl CardPort for SystemPort {
    fn set_dtr(&mut self, on: bool) -> io::Result<()> {
        self.0.write_data_terminal_ready(on).map_err(io::Error::from)
    }
    fn try_clone_port(&self) -> io::Result<Box<dyn CardPort>> {
        let clone = self.0.try_clone().map_err(io::Error::from)?;
        Ok(Box::new(SystemPort(clone)))
    }
}

/// serialport reports a port in use as NoDevice: EBUSY from TIOCEXCL or a held flock on Linux
/// and macOS, ERROR_ACCESS_DENIED on Windows (where a missing port gives it too).
pub fn open_error(error: serialport::Error) -> OpenError {
    match error.kind() {
        serialport::ErrorKind::NoDevice => OpenError::Busy,
        serialport::ErrorKind::Io(io::ErrorKind::PermissionDenied) => OpenError::Permission,
        serialport::ErrorKind::Io(io::ErrorKind::NotFound) => OpenError::NotFound,
        _ => OpenError::Failed,
    }
}

/// The paths of the cards among the ports (path, USB vendor and product id): USB 2886:8057 only.
/// macOS lists every port twice; only its /dev/cu.* name is for programs that open the port
/// (the /dev/tty.* one waits for a carrier).
pub fn card_paths(ports: &[(String, Option<(u16, u16)>)], macos: bool) -> Vec<String> {
    ports
        .iter()
        .filter(|(_, usb)| *usb == Some((USB_VENDOR_ID, USB_PRODUCT_ID)))
        .filter(|(path, _)| !macos || path.starts_with("/dev/cu."))
        .map(|(path, _)| path.clone())
        .collect()
}

/// The cards plugged in now.
pub fn list() -> Vec<String> {
    let ports: Vec<(String, Option<(u16, u16)>)> = serialport::available_ports()
        .unwrap_or_default()
        .into_iter()
        .map(|port| {
            let usb = match port.port_type {
                serialport::SerialPortType::UsbPort(usb) => Some((usb.vid, usb.pid)),
                _ => None,
            };
            (port.port_name, usb)
        })
        .collect();
    card_paths(&ports, cfg!(target_os = "macos"))
}

/// An open card: the port for writing here, a thread reading it.
pub struct Connection {
    writer: Mutex<Box<dyn CardPort>>,
    stop: Arc<AtomicBool>,
    reader: Option<JoinHandle<()>>,
}

impl Connection {
    /// Opens the card's port at BAUD_RATE with DTR on (the card writes its console only then) and
    /// passes its lines to `events` from a thread, then Closed.
    pub fn open(
        opener: &dyn Opener,
        path: &str,
        events: impl Fn(Event) + Send + 'static,
    ) -> Result<Connection, OpenError> {
        let mut port = opener.open(path, BAUD_RATE)?;
        port.set_dtr(true).map_err(|_| OpenError::Failed)?;
        let reading = port.try_clone_port().map_err(|_| OpenError::Failed)?;
        let stop = Arc::new(AtomicBool::new(false));
        let stopped = Arc::clone(&stop);
        let reader = std::thread::spawn(move || read_lines(reading, &stopped, events));
        Ok(Connection {
            writer: Mutex::new(port),
            stop,
            reader: Some(reader),
        })
    }

    /// Sends a command of test/commands.json with its newline; anything else never reaches the card.
    pub fn send(&self, command: &str) -> Result<(), SendError> {
        if !commands::allowed(command) {
            return Err(SendError::Refused);
        }
        let mut port = self.writer.lock().map_err(|_| SendError::Failed)?;
        port.write_all(format!("{command}\n").as_bytes())
            .and_then(|()| port.flush())
            .map_err(|_| SendError::Failed)
    }

    /// Stops the reading and closes the port (dropping does the same).
    pub fn close(mut self) {
        self.stop_reading();
    }

    fn stop_reading(&mut self) {
        self.stop.store(true, Ordering::Relaxed);
        if let Some(reader) = self.reader.take() {
            let _ = reader.join(); // within PORT_TIMEOUT
        }
    }
}

impl Drop for Connection {
    fn drop(&mut self) {
        self.stop_reading();
    }
}

fn read_lines(mut port: Box<dyn CardPort>, stop: &AtomicBool, events: impl Fn(Event)) {
    let mut lines = LineAssembler::default();
    let mut buffer = [0u8; 256];
    let error = loop {
        if stop.load(Ordering::Relaxed) {
            break None;
        }
        match port.read(&mut buffer) {
            Ok(0) => break Some("the card's port closed".to_string()),
            Ok(count) => {
                for text in lines.push(&buffer[..count]) {
                    events(Event::Line { text });
                }
            }
            Err(error) if matches!(error.kind(), io::ErrorKind::TimedOut | io::ErrorKind::Interrupted) => {}
            Err(error) => break Some(format!("the card's port failed: {error}")),
        }
    };
    events(Event::Closed { error });
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::collections::VecDeque;
    use std::time::Instant;

    /// A port that gives the queued reads, then times out, and records what it gets.
    #[derive(Clone, Default)]
    struct StandIn {
        reads: Arc<Mutex<VecDeque<io::Result<Vec<u8>>>>>,
        written: Arc<Mutex<Vec<u8>>>,
        dtr: Arc<Mutex<Option<bool>>>,
    }

    impl Read for StandIn {
        fn read(&mut self, buffer: &mut [u8]) -> io::Result<usize> {
            match self.reads.lock().unwrap().pop_front() {
                Some(Ok(bytes)) => {
                    buffer[..bytes.len()].copy_from_slice(&bytes);
                    Ok(bytes.len())
                }
                Some(Err(error)) => Err(error),
                None => {
                    std::thread::sleep(Duration::from_millis(5));
                    Err(io::ErrorKind::TimedOut.into())
                }
            }
        }
    }

    impl Write for StandIn {
        fn write(&mut self, bytes: &[u8]) -> io::Result<usize> {
            self.written.lock().unwrap().extend_from_slice(bytes);
            Ok(bytes.len())
        }
        fn flush(&mut self) -> io::Result<()> {
            Ok(())
        }
    }

    impl CardPort for StandIn {
        fn set_dtr(&mut self, on: bool) -> io::Result<()> {
            *self.dtr.lock().unwrap() = Some(on);
            Ok(())
        }
        fn try_clone_port(&self) -> io::Result<Box<dyn CardPort>> {
            Ok(Box::new(self.clone()))
        }
    }

    struct StandInOpener {
        port: StandIn,
        opened: Arc<Mutex<Vec<(String, u32)>>>,
    }

    impl Opener for StandInOpener {
        fn open(&self, path: &str, baud_rate: u32) -> Result<Box<dyn CardPort>, OpenError> {
            self.opened.lock().unwrap().push((path.to_string(), baud_rate));
            Ok(Box::new(self.port.clone()))
        }
    }

    fn collector() -> (Arc<Mutex<Vec<Event>>>, impl Fn(Event) + Send + 'static) {
        let events = Arc::new(Mutex::new(Vec::new()));
        let sink = Arc::clone(&events);
        (events, move |event| sink.lock().unwrap().push(event))
    }

    fn wait_for_closed(events: &Mutex<Vec<Event>>) {
        let start = Instant::now();
        while !events
            .lock()
            .unwrap()
            .iter()
            .any(|event| matches!(event, Event::Closed { .. }))
        {
            assert!(start.elapsed() < Duration::from_secs(2), "no Closed event");
            std::thread::sleep(Duration::from_millis(5));
        }
    }

    #[test]
    fn opens_at_115200_with_dtr_and_passes_whole_lines() {
        let port = StandIn::default();
        port.reads
            .lock()
            .unwrap()
            .extend([Ok(b"status: format=1\r\nstat".to_vec()), Ok(b"us: end\r\n".to_vec())]);
        let opener = StandInOpener {
            port: port.clone(),
            opened: Arc::default(),
        };
        let (events, sink) = collector();
        let connection = Connection::open(&opener, "/dev/ttyACM0", sink).unwrap();
        assert_eq!(*opener.opened.lock().unwrap(), [("/dev/ttyACM0".to_string(), 115_200)]);
        assert_eq!(*port.dtr.lock().unwrap(), Some(true));
        std::thread::sleep(Duration::from_millis(50));
        connection.close();
        assert_eq!(
            *events.lock().unwrap(),
            [
                Event::Line {
                    text: "status: format=1".into()
                },
                Event::Line {
                    text: "status: end".into()
                },
                Event::Closed { error: None },
            ]
        );
    }

    #[test]
    fn an_unplugged_card_ends_the_reading_with_its_error() {
        let port = StandIn::default();
        port.reads
            .lock()
            .unwrap()
            .extend([Ok(b"v\n".to_vec()), Err(io::Error::other("Input/output error"))]);
        let opener = StandInOpener {
            port,
            opened: Arc::default(),
        };
        let (events, sink) = collector();
        let connection = Connection::open(&opener, "/dev/ttyACM0", sink).unwrap();
        wait_for_closed(&events);
        assert_eq!(
            *events.lock().unwrap(),
            [
                Event::Line { text: "v".into() },
                Event::Closed {
                    error: Some("the card's port failed: Input/output error".into())
                },
            ]
        );
        drop(connection); // the thread has ended: nothing more
        assert_eq!(events.lock().unwrap().len(), 2);
    }

    #[test]
    fn only_allowed_commands_are_written() {
        let port = StandIn::default();
        let opener = StandInOpener {
            port: port.clone(),
            opened: Arc::default(),
        };
        let (_events, sink) = collector();
        let connection = Connection::open(&opener, "/dev/ttyACM0", sink).unwrap();
        assert_eq!(connection.send("status"), Ok(()));
        assert_eq!(connection.send("tz Europe/Warsaw"), Ok(()));
        for refused in ["dfu", "off", "link off", "status\ndfu", "tz europe/warsaw", ""] {
            assert_eq!(connection.send(refused), Err(SendError::Refused), "{refused:?}");
        }
        assert_eq!(
            String::from_utf8(port.written.lock().unwrap().clone()).unwrap(),
            "status\ntz Europe/Warsaw\n"
        );
        assert_eq!(SendError::Refused.word(), "refused");
    }

    #[test]
    fn only_cards_are_listed_and_on_macos_only_their_callout_name() {
        let card = Some((0x2886, 0x8057));
        let ports = vec![
            ("/dev/ttyACM0".to_string(), card),
            ("/dev/ttyACM1".to_string(), Some((0x2886, 0x0057))), // the card's bootloader
            ("/dev/ttyUSB0".to_string(), Some((0x10c4, 0xea60))),
            ("/dev/ttyS0".to_string(), None),
            ("/dev/cu.usbmodem101".to_string(), card),
            ("/dev/tty.usbmodem101".to_string(), card),
        ];
        assert_eq!(
            card_paths(&ports, false),
            ["/dev/ttyACM0", "/dev/cu.usbmodem101", "/dev/tty.usbmodem101"]
        );
        assert_eq!(card_paths(&ports, true), ["/dev/cu.usbmodem101"]);
    }

    #[test]
    fn open_errors_become_the_words_the_page_explains() {
        let error = |kind| serialport::Error::new(kind, "test");
        assert_eq!(open_error(error(serialport::ErrorKind::NoDevice)).word(), "busy");
        assert_eq!(
            open_error(error(serialport::ErrorKind::Io(io::ErrorKind::PermissionDenied))).word(),
            "permission"
        );
        assert_eq!(
            open_error(error(serialport::ErrorKind::Io(io::ErrorKind::NotFound))).word(),
            "not-found"
        );
        assert_eq!(open_error(error(serialport::ErrorKind::Unknown)).word(), "failed");
    }

    /// The card page in Chromium takes an empty read for a lost device when a program left the
    /// port with VMIN=0 (as pyserial does). serialport opens it raw: VMIN=1, VTIME=0, also after
    /// the app. A pseudo terminal stands in for the card.
    #[cfg(target_os = "linux")]
    #[test]
    fn on_linux_the_port_keeps_vmin_1_after_the_app() {
        use std::ffi::CStr;

        let (mut leader, mut follower) = (0, 0);
        let opened = unsafe {
            libc::openpty(
                &mut leader,
                &mut follower,
                std::ptr::null_mut(),
                std::ptr::null(),
                std::ptr::null(),
            )
        };
        assert_eq!(opened, 0);
        let path = unsafe { CStr::from_ptr(libc::ttyname(follower)) }
            .to_str()
            .unwrap()
            .to_string();
        // Read through the test's own handle, which stays open, so the settings are kept.
        let vmin_vtime = || {
            let mut termios: libc::termios = unsafe { std::mem::zeroed() };
            assert_eq!(unsafe { libc::tcgetattr(follower, &mut termios) }, 0);
            (termios.c_cc[libc::VMIN], termios.c_cc[libc::VTIME])
        };
        {
            let mut termios: libc::termios = unsafe { std::mem::zeroed() };
            assert_eq!(unsafe { libc::tcgetattr(follower, &mut termios) }, 0);
            termios.c_cc[libc::VMIN] = 0; // as pyserial leaves it
            assert_eq!(unsafe { libc::tcsetattr(follower, libc::TCSANOW, &termios) }, 0);
        }
        let port = SystemPorts.open(&path, BAUD_RATE).expect("the pseudo terminal opens");
        assert_eq!(vmin_vtime(), (1, 0));
        drop(port);
        assert_eq!(vmin_vtime(), (1, 0));
        unsafe {
            libc::close(follower);
            libc::close(leader);
        }
    }
}
