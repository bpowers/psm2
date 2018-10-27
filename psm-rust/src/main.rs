// Copyright 2018 Bobby Powers. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#![feature(alloc_system)]

extern crate alloc_system;

//extern crate libc;
//use libc::geteuid;

use std::cmp::{Eq, Ordering};
use std::fs::{self, File};
use std::io::{BufRead, BufReader, Error, ErrorKind, Read, Result};

const PROC_PATH: &'static str = "/proc";

const TY_PSS: &'static str = "Pss:";
const TY_SWAP: &'static str = "Swap:";
const TY_PRIVATE_CLEAN: &'static str = "Private_Clean:";
const TY_PRIVATE_DIRTY: &'static str = "Private_Dirty:";

struct CmdStat {
    name: String,
    pid: i32,
    pss: f32,
    shared: f32,
    swap: f32,
    count: i32, // used when summing up
}

impl CmdStat {
    fn new(pid: i32) -> Result<CmdStat> {
        let name = proc_name(pid)?;

        let mut stats = CmdStat {
            name: name,
            pid: pid,
            pss: 0.0,
            shared: 0.0,
            swap: 0.0,
            count: 1,
        };

        stats.collect_memory_usage()?;

        Ok(stats)
    }

    fn collect_memory_usage(&mut self) -> Result<()> {
        let path = format!("/proc/{}/smaps_rollup", self.pid);
        let file = File::open(path)?;

        let mut private: f32 = 0.0;

        for line in BufReader::new(file).lines() {
            let line = line.unwrap();
            if line.starts_with(TY_PSS) {
                if let Ok(n) = parse_line(&line) {
                    self.pss += n;
                }
            } else if line.starts_with(TY_SWAP) {
                if let Ok(n) = parse_line(&line) {
                    self.swap += n;
                }
            } else if line.starts_with(TY_PRIVATE_CLEAN) || line.starts_with(TY_PRIVATE_DIRTY) {
                if let Ok(n) = parse_line(&line) {
                    private += n;
                }
            }
        }

        self.shared = self.pss - private;

        Ok(())
    }
}

fn parse_line(line: &str) -> Result<f32> {
    let kbs = &line[16..24];
    let num: f32 = kbs
        .trim_start()
        .parse()
        .map_err(|_e| Error::new(ErrorKind::Other, "parse error"))?;
    Ok(num)
}

impl PartialEq for CmdStat {
    fn eq(&self, other: &CmdStat) -> bool {
        self.name == other.name
    }
}
impl Eq for CmdStat {}

impl PartialOrd for CmdStat {
    fn partial_cmp(&self, other: &CmdStat) -> Option<Ordering> {
        Some(self.cmp(other))
    }
}

impl Ord for CmdStat {
    fn cmp(&self, other: &CmdStat) -> Ordering {
        self.name.cmp(&other.name)
    }
}

#[inline(never)]
fn is_digit(d: char) -> bool {
    d >= '0' && d <= '9'
}

#[inline(never)]
fn first_char(name: &std::ffi::OsStr) -> char {
    use std::os::unix::ffi::OsStrExt;
    name.as_bytes()[0] as char
}

fn is_dir(md: fs::Metadata) -> Result<fs::Metadata> {
    if md.is_dir() {
        Ok(md)
    } else {
        Err(Error::new(ErrorKind::NotFound, "not a directory"))
    }
}

#[inline(never)]
fn get_pids() -> Result<Vec<i32>> {
    let dir = fs::read_dir(PROC_PATH)?;

    // TODO: look at filter_map
    let pids: Vec<i32> = dir
        .filter(|e| e.is_ok()) // ignore bad dirent results
        .map(|e| e.unwrap()) // make this a list of dirents
        .filter(|e| is_digit(first_char(&e.file_name())))
        .filter(|e| fs::metadata(e.path()).and_then(|md| is_dir(md)).is_ok())
        .map(|e| i32::from_str_radix(e.file_name().to_str().unwrap(), 10).unwrap())
        .collect();

    Ok(pids)
}

fn proc_cmdline(pid: i32) -> Result<String> {
    let mut buf = String::new();
    let path = format!("/proc/{}/cmdline", pid);

    // TODO: we don't really care about reading all of the
    // cmdline, just the first 1024 chars is enough.
    let mut f = File::open(path)?;

    f.read_to_string(&mut buf)?;

    Ok(buf)
}

fn proc_name(pid: i32) -> Result<String> {
    let path = format!("/proc/{}/exe", pid);

    let owned_path = fs::read_link(path)?;
    let full_path = owned_path.to_string_lossy();
    let cmdline = proc_cmdline(pid)?;

    let result = if cmdline.starts_with(&*full_path) {
        // the basename (last component) of the path
        owned_path
            .file_name()
            .unwrap()
            .to_string_lossy()
            .to_string()
    } else {
        cmdline
    };

    Ok(result)
}

fn cmdstats_for(pids: Vec<i32>) -> Vec<CmdStat> {
    pids.into_iter()
        .filter_map(|pid| CmdStat::new(pid).ok())
        .collect()
}

fn main() {
    // command line flag parsing

    // check euid
    // unsafe {
    // 	let uid = geteuid();
    // 	if uid != 0 {
    // 	    println!("we aren't root");
    // 	}
    // }

    let pids_r = get_pids();
    if let Err(err) = pids_r {
        println!("get_pids: {}", err);
        return;
    }

    let pids = pids_r.unwrap();

    let mut stats = cmdstats_for(pids);

    stats.sort_unstable();

    stats.dedup_by(|a, b| {
        if a.name != b.name {
            return false;
        }
        b.pss += a.pss;
        b.shared += a.shared;
        b.swap += a.swap;
        b.count += 1;
        true
    });

    stats.sort_unstable_by(|a, b| a.pss.partial_cmp(&b.pss).unwrap_or(Ordering::Equal));

    // TODO: this could be a single iteration
    let total_pss = stats.iter().fold(0.0, |sum, stat| sum + stat.pss);
    let total_shared = stats.iter().fold(0.0, |sum, stat| sum + stat.shared);
    let total_swap = stats.iter().fold(0.0, |sum, stat| sum + stat.swap);

    for cmd in &stats {
        let swap = "";
        println!("{:10.1}{:10.1}{:10}\t{} ({})", cmd.pss/1024.0, cmd.shared/1024.0, swap, cmd.name, cmd.count)
    }

    // print_results
    println!("#{:9.1}{:20.1}\tTOTAL USED BY PROCESSES", total_pss/1024.0, total_swap/1024.0);
}
