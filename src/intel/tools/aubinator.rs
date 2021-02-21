extern crate aub_rs;
extern crate getopts;
extern crate intelcommon_rs;
extern crate inteldev_rs;

use getopts::Options;
use std::env;
use std::ffi::{CStr, CString};
use std::io::Write;
use std::os::raw::{c_char, c_int, c_void};
use std::process::{Command, Stdio};
use std::str::FromStr;

use intelcommon_rs::*;

macro_rules! outln {
    () => (write!(get_logger(), "\n").unwrap());
    ($($arg:tt)*) => ({
        writeln!(get_logger(), $($arg)*).unwrap();
    })
}

fn handle_execlist_write(
    user_data: *mut c_void,
    engine: u16,
    context_descriptor: u64,
) {
    outln!("handle_execlist_write called!");
}

#[no_mangle]
unsafe extern "C" fn handle_execlist_write_c(
    user_data: *mut c_void,
    engine: u16,
    context_descriptor: u64,
) {
    handle_execlist_write(user_data, engine, context_descriptor)
}

fn handle_ring_write(user_data: *mut c_void, engine: u16, data: *const c_void, data_len: u32) {
    outln!("handle_ring_write called!");
    let batch_ctx = get_batch_ctx();
    let mut mem = get_mem();
    batch_ctx.user_data = mem as *mut _ as *mut c_void;
    batch_ctx.get_bo = Some(get_legacy_bo_c);
    batch_ctx.engine = engine as i32;
    unsafe {
        intelcommon_rs::gen_print_batch(batch_ctx, data as *const u32, data_len, 0, false);
        aub_rs::aub_mem_clear_bo_maps(mem);
    }
}

#[no_mangle]
unsafe extern "C" fn handle_ring_write_c(
    user_data: *mut c_void,
    engine: u16,
    data: *const c_void,
    data_len: u32,
) {
    handle_ring_write(user_data, engine, data, data_len);
}

#[no_mangle]
unsafe extern "C" fn get_legacy_bo_c(
    user_data: *mut c_void,
    ppgtt: bool,
    addr: u64,
) -> intelcommon_rs::gen_batch_decode_bo {
    let r = aub_rs::aub_mem_get_ggtt_bo(user_data, addr);
    std::mem::transmute::<_, intelcommon_rs::gen_batch_decode_bo>(r)
}

fn aubinator_init(user_data: *mut c_void, pci_id: i32, app_name: &str) {
    outln!("App name: {}, pci-id: 0x{:04x}", app_name, pci_id);

    let devinfo = get_devinfo_mut() as *mut _ as *mut inteldev_rs::gen_device_info;
    let res = unsafe { inteldev_rs::gen_get_device_info_from_pci_id(pci_id, devinfo) };
    if !res {
        eprintln!("can't find device information: pci_id=0x{:x}\n", pci_id);
        std::process::exit(-1);
    }

    let options = get_options();
    let mut batch_flags: gen_batch_decode_flags = 0;
    if options.color != Some(false) {
        batch_flags |= gen_batch_decode_flags_GEN_BATCH_DECODE_IN_COLOR;
    }
    if !options.headers {
        batch_flags |= gen_batch_decode_flags_GEN_BATCH_DECODE_FULL;
    }
    if !options.no_offsets {
        batch_flags |= gen_batch_decode_flags_GEN_BATCH_DECODE_OFFSETS;
    }

    let devinfo = get_devinfo() as *const _ as *const intelcommon_rs::gen_device_info;
    let xml_dir = if let Some(x) = &options.xml_dir {
        x.as_ptr() as *const _ as *const i8
    } else {
        std::ptr::null()
    };

    unsafe {
        gen_batch_decode_ctx_init(
            get_batch_ctx(),
            devinfo,
            stdout,
            batch_flags,
            xml_dir,
            None,
            None,
            std::ptr::null_mut() as *mut _ as *mut c_void,
        );
    }
}

#[no_mangle]
unsafe extern "C" fn aubinator_init_c(user_data: *mut c_void, pci_id: i32, app_name: *const i8) {
    let app_name = CStr::from_ptr(app_name).to_str().unwrap_or("");
    aubinator_init(user_data, pci_id, app_name);
}

pub fn main() {
    let cmd_line = parse_options();
    let cmd_line = if let Some(c) = cmd_line {
        c
    } else {
        std::process::exit(-1);
    };

    unsafe {
        OPTIONS = Some(cmd_line);
    }

    unsafe {
        DEVINFO = Some(inteldev_rs::gen_device_info {
            ..Default::default()
        });
    }

    unsafe {
        BATCH_CTX = Some(gen_batch_decode_ctx {
            ..Default::default()
        });
    }

    unsafe {
        LOGGER = Some(Logger::new(get_options().disable_pager));
    }
    outln!("Aubinator! I'll be back!");

    unsafe {
        MEM = Some(aub_rs::aub_mem {
            ..Default::default()
        });
    }
    if !unsafe { aub_rs::aub_mem_init(get_mem()) } {
        outln!("Unable to create GTT");
        std::process::exit(-1);
    }

    let mut ar = aub_rs::aub_read {
        user_data: get_mem() as *mut _ as *mut c_void,
        info: Some(aubinator_init_c),
        local_write: Some(aub_rs::aub_mem_local_write),
        phys_write: Some(aub_rs::aub_mem_phys_write),
        ggtt_write: Some(aub_rs::aub_mem_ggtt_write),
        ggtt_entry_write: Some(aub_rs::aub_mem_ggtt_entry_write),
        execlist_write: Some(handle_execlist_write_c),
        ring_write: Some(handle_ring_write_c),
        ..Default::default()
    };

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

    unsafe {
        LOGGER = None;
    }
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

fn gen_device_name_to_pci_id(name: &str) -> i32 {
    let c_name = CString::new(name);
    if let Ok(c_name) = c_name {
        unsafe { inteldev_rs::gen_device_name_to_pci_device_id(c_name.as_ptr()) }
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
    let gen = match m.opt_str("gen") {
        Some(g) => gen_device_name_to_pci_id(&g),
        _ => -1,
    };
    let color = match m.opt_str("color") {
        Some(clr) => match clr.as_str() {
            "auto" => None,
            "always" => Some(true),
            "never" => Some(false),
            _ => {
                help = true;
                None
            }
        },
        _ => None,
    };
    let max_vbo_lines = match m.opt_str("max-vbo-lines") {
        Some(m) => {
            let m = u64::from_str(m.as_str());
            match m {
                Ok(m) => Some(m),
                _ => {
                    help = true;
                    None
                }
            }
        }
        _ => None,
    };
    let xml_dir = m.opt_str("xml");
    if help {
        print_usage(&program, opts);
        return None;
    }
    let headers = !help && m.opt_present("headers");
    let disable_pager = !help && m.opt_present("no-pager");
    let no_offsets = !help && m.opt_present("no-offsets");
    Some(CmdLine {
        gen,
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
            Some(
                Command::new("less")
                    .arg("-FRSi")
                    .stdin(Stdio::piped())
                    .spawn()
                    .expect("Failed to execute pager!"),
            )
        };

        Logger { pager }
    }

    fn write_fmt(&mut self, fmt: std::fmt::Arguments<'_>) -> std::io::Result<()> {
        match &mut self.pager {
            Some(p) => {
                let dst = p.stdin.as_mut().expect("Failed to open stdin");
                dst.write_fmt(fmt)
            }
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
            p.wait()
                .expect("Could not wait for pager process to finish!");
        }
    }
}

static mut OPTIONS: Option<CmdLine> = None;

fn get_options() -> &'static CmdLine {
    unsafe { OPTIONS.as_ref().unwrap() }
}

static mut LOGGER: Option<Logger> = None;

fn get_logger() -> &'static mut Logger {
    unsafe { LOGGER.as_mut().unwrap() }
}

static mut DEVINFO: Option<inteldev_rs::gen_device_info> = None;

fn get_devinfo_mut() -> &'static mut inteldev_rs::gen_device_info {
    unsafe { DEVINFO.as_mut().unwrap() }
}

fn get_devinfo() -> &'static inteldev_rs::gen_device_info {
    unsafe { DEVINFO.as_ref().unwrap() }
}

static mut BATCH_CTX: Option<gen_batch_decode_ctx> = None;

fn get_batch_ctx() -> &'static mut gen_batch_decode_ctx {
    unsafe { BATCH_CTX.as_mut().unwrap() }
}

static mut MEM: Option<aub_rs::aub_mem> = None;

fn get_mem() -> &'static mut aub_rs::aub_mem {
    unsafe { MEM.as_mut().unwrap() }
}
