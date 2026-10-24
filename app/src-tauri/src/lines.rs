//! The card's console output, split into lines for the page.

/// A line is cut here if the card sends no newline for that long, so a broken stream cannot fill
/// the memory. The card's own lines are at most 128 bytes (src/log.h LINE_MAX).
pub const MAX_LINE: usize = 1024;

/// Collects bytes until "\n" ends a line. A "\r" before it is dropped, as the page's LineSplitter
/// does, and bytes that are not UTF-8 (rare GPS noise in the log) become U+FFFD.
#[derive(Default)]
pub struct LineAssembler {
    pending: Vec<u8>,
}

impl LineAssembler {
    /// The lines completed by these bytes, oldest first.
    pub fn push(&mut self, bytes: &[u8]) -> Vec<String> {
        let mut lines = Vec::new();
        for &byte in bytes {
            if byte == b'\n' {
                lines.push(self.take());
            } else {
                self.pending.push(byte);
                if self.pending.len() >= MAX_LINE {
                    lines.push(self.take());
                }
            }
        }
        lines
    }

    fn take(&mut self) -> String {
        if self.pending.last() == Some(&b'\r') {
            self.pending.pop();
        }
        let line = String::from_utf8_lossy(&self.pending).into_owned();
        self.pending.clear();
        line
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn lines_come_whole_from_pieces() {
        let mut lines = LineAssembler::default();
        assert!(lines.push(b"gpxcam-t1000e 1.4").is_empty());
        assert_eq!(
            lines.push(b".0 (git 1234567)\r\nreset: 0x00000004 (SREQ)\r\nstat"),
            ["gpxcam-t1000e 1.4.0 (git 1234567)", "reset: 0x00000004 (SREQ)"]
        );
        assert_eq!(lines.push(b"us: end\n\n"), ["status: end", ""]);
    }

    #[test]
    fn a_carriage_return_stays_inside_a_line() {
        let mut lines = LineAssembler::default();
        assert_eq!(lines.push(b"a\rb\r\n"), ["a\rb"]);
    }

    #[test]
    fn bytes_that_are_not_utf8_do_not_stop_the_line() {
        let mut lines = LineAssembler::default();
        assert_eq!(lines.push(b"gps: \xff\xfe ok\n"), ["gps: \u{fffd}\u{fffd} ok"]);
    }

    #[test]
    fn a_line_without_end_is_cut() {
        let mut lines = LineAssembler::default();
        let long = vec![b'x'; MAX_LINE + 5];
        let cut = lines.push(&long);
        assert_eq!(cut.len(), 1);
        assert_eq!(cut[0].len(), MAX_LINE);
        assert_eq!(lines.push(b"\n"), ["xxxxx"]);
    }
}
