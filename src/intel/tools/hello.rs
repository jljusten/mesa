use std::io::Write;
use std::process::{Command, Stdio};

pub fn main() {
    let mut logger = Logger::new();
    show_gens(&mut logger);
}

struct Logger {
    pager: std::process::Child,
}

impl Logger {
    fn new() -> Logger {
        Logger {
            pager: Command::new("less")
                .arg("-FRSi")
                .stdin(Stdio::piped())
                .spawn()
                .expect("Failed to execute pager!"),
        }

    }

    fn write_fmt(&mut self, fmt: std::fmt::Arguments<'_>) -> std::io::Result<()> {
        let stdin = self.pager.stdin.as_mut().expect("Failed to open stdin");
        stdin.write_fmt(fmt)
    }
}

impl Drop for Logger {
    fn drop(&mut self) {
        self.pager.wait()
            .expect("Could not wait for pager process to finish!");
    }
}

fn show_gens(logger: &mut Logger) {
    for i in 1..500 {
        writeln!(logger, "Hello, Intel gen {}!", i).unwrap();
    }
}
