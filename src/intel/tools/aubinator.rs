extern crate getopts;

use getopts::Options;
use std::env;
use std::io::Write;
use std::process::{Command, Stdio};
use std::str::FromStr;

pub fn main() {
    let cmd_line = parse_options();
    let cmd_line = if let Some(c) = cmd_line {
        c
    } else {
        std::process::exit(-1);
    };

    println!("Aubinator! I'll be back!");
}

#[derive(Debug)]
struct CmdLine {
    gen: String,
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
            Some(g) => g,
            _ => "".to_string()
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
