extern crate aub_rs;
extern crate getopts;

use getopts::Options;
use std::env;
use std::ffi::{CStr, CString};
use std::io::Write;
use std::os::raw::{c_char, c_int, c_void};
use std::process::{Command, Stdio};
use std::str::FromStr;

macro_rules! outln {
    () => (write!(get_logger(), "\n").unwrap());
    ($($arg:tt)*) => ({
        writeln!(get_logger(), $($arg)*).unwrap();
    })
}

fn aubinator_init(user_data: *mut c_void, pci_id: i32, app_name: &str)
{
    outln!("App name: {}, pci-id: 0x{:04x}", app_name, pci_id);
}

#[no_mangle]
unsafe extern "C" fn aubinator_init_c(user_data: *mut c_void, pci_id: i32, app_name: *const i8)
{
    let app_name = CStr::from_ptr(app_name).to_str().unwrap_or("");
    aubinator_init(user_data, pci_id, app_name);
}

pub fn main()
{
    let cmd_line = parse_options();
    let cmd_line = if let Some(c) = cmd_line {
        c
    } else {
        std::process::exit(-1);
    };

    unsafe { LOGGER = Some(Logger::new(cmd_line.disable_pager)); }
    outln!("Aubinator! I'll be back!");

    let mut mem = aub_rs::aub_mem { ..Default::default() };
    if !unsafe { aub_rs::aub_mem_init(&mut mem) } {
        outln!("Unable to create GTT");
        std::process::exit(-1);
    }

    let mut ar = aub_rs::aub_read {
        user_data: &mut mem as *mut _ as *mut c_void,
        info: Some(aubinator_init_c),
        local_write: Some(aub_rs::aub_mem_local_write),
        phys_write: Some(aub_rs::aub_mem_phys_write),
        ggtt_write: Some(aub_rs::aub_mem_ggtt_write),
        ggtt_entry_write: Some(aub_rs::aub_mem_ggtt_entry_write),
        ..Default::default()
    };
    //outln!("ar: {:?}", ar);

    let fname = "1.aub";
    let c_fname = CString::new(fname).unwrap();
    let aub_reader = unsafe { aub_rs::aub_reader_open(c_fname.as_ptr()) };
    if !aub_reader.is_null() {
        unsafe {
            aub_rs::aub_reader_readall(aub_reader, &mut ar);
            aub_rs::aub_reader_close(aub_reader);
        }
    } else {
        outln!("failed to open: {}", fname);
        std::process::exit(-1);
    }

    unsafe { LOGGER = None; }
}

#[derive(Debug)]
struct CmdLine {
    gen: i32,
    headers: bool,
    color: Option<bool>,
    max_vbo_lines: Option<u64>,
    disable_pager: bool,
    no_offsets: bool,
    xml_dir: Option<String>,
}

fn print_usage(program: &str, opts: Options) {
    let brief = format!("Usage: {} [OPTION]... FILE", program);
    print!("{}", opts.usage(&brief));
}

#[link(name = "intel_dev")]
extern "C" {
    fn gen_device_name_to_pci_device_id(dev_name: *const c_char) -> c_int;
}

fn gen_device_name_to_pci_id(name: &str) -> i32 {
    let c_name = CString::new(name);
    if let Ok(c_name) = c_name {
        unsafe { gen_device_name_to_pci_device_id(c_name.as_ptr()) }
    } else {
        -1
    }
}

fn parse_options() -> Option<CmdLine> {
    let args: Vec<String> = env::args().collect();
    let program = args[0].clone();
    let mut opts = Options::new();
    opts.optflag("h", "help", "display this help and exit");
    opts.optopt(
        "",
        "gen",
        "decode for given platform (3 letter platform name)",
        "",
    );
    opts.optflag("", "headers", "decode only command headers");
    opts.optopt(
        "",
        "color",
        "colorize the output; WHEN can be 'auto' (default if omitted), 'always', or 'never'",
        "WHEN",
    );
    opts.optopt(
        "",
        "max-vbo-lines",
        "limit the number of decoded VBO lines",
        "N",
    );
    opts.optflag("", "no-pager", "don't launch pager");
    opts.optflag("", "no-offsets", "don't print instruction offsets");
    opts.optopt(
        "",
        "xml",
        "load hardware xml description from directory DIR",
        "DIR",
    );
    let m = match opts.parse(&args[1..]) {
        Ok(m) => m,
        Err(f) => panic!(f.to_string()),
    };
    let mut help = false;
    if m.opt_present("h") {
        help = true;
    }
    let gen =
        match m.opt_str("gen") {
            Some(g) => gen_device_name_to_pci_id(&g),
            _ => -1,
        };
    let color =
        match m.opt_str("color") {
            Some(clr) => {
                match clr.as_str() {
                    "auto" => None,
                    "always" => Some(true),
                    "never" => Some(false),
                    _ => {
                        help = true;
                        None
                    },
                }
            },
            _ => None
        };
    let max_vbo_lines =
        match m.opt_str("max-vbo-lines") {
            Some(m) => {
                let m = u64::from_str(m.as_str());
                match m {
                    Ok(m) => Some(m),
                    _ => {
                        help = true;
                        None
                    },
                }
            },
            _ => None
        };
    let xml_dir = m.opt_str("xml");
    if help {
        print_usage(&program, opts);
        return None;
    }
    let headers = !help && m.opt_present("headers");
    let disable_pager = !help && m.opt_present("no-pager");
    let no_offsets = !help && m.opt_present("no-offsets");
    Some(CmdLine { gen,
                   headers,
                   color,
                   max_vbo_lines,
                   disable_pager,
                   no_offsets,
                   xml_dir,
    })
}

struct Logger {
    pager: Option<std::process::Child>,
}

impl Logger {
    fn new(disable_pager: bool) -> Logger {
        let pager = if disable_pager {
            None
        } else {
            Some(Command::new("less")
                .arg("-FRSi")
                .stdin(Stdio::piped())
                .spawn()
                .expect("Failed to execute pager!"))
        };

        Logger { pager}
    }

    fn write_fmt(&mut self, fmt: std::fmt::Arguments<'_>) -> std::io::Result<()> {
        match &mut self.pager {
            Some(p) => {
                let dst = p.stdin.as_mut().expect("Failed to open stdin");
                dst.write_fmt(fmt)
            },
            _ => {
                let dst = std::io::stdout();
                let mut dst = dst.lock();
                dst.write_fmt(fmt)
            }
        }
    }
}

impl Drop for Logger {
    fn drop(&mut self) {
        if let Some(p) = &mut self.pager {
            p.wait().expect("Could not wait for pager process to finish!");
        }
    }
}

static mut LOGGER: Option<Logger> = None;

fn get_logger() -> &'static mut Logger {
    unsafe { LOGGER.as_mut().unwrap() }
}
