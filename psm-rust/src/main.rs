// Copyright 2018 Bobby Powers. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#![feature(alloc_system)]

// extern crate alloc_system;

//extern crate libc;
//use libc::geteuid;

use std::cmp::{Eq, Ordering};
use std::fs::{self, File};
use std::io::{BufRead, BufReader, Error, ErrorKind, Read, Result};

struct Lines<'a> {
    reader: BufReader<File>,
    buffer: &'a mut Vec<u8>,
}

impl<'a> Lines<'a> {
    fn new(file: File, buffer: &'a mut Vec<u8>) -> Lines {
        Lines {
            reader: BufReader::new(file),
            buffer: buffer,
        }
    }
}

impl<'a> Iterator for Lines<'a> {
    type Item = &'a [u8];

    fn next<'b>(&'b mut self) -> Option<Self::Item> {
        const NEWLINE: u8 = b'\n';

        self.buffer.truncate(0);

        match self.reader.read_until(NEWLINE, &mut self.buffer) {
            Ok(len) => {
                if len > 0 {
                    let by: &'b [u8] = self.buffer.as_slice();
                    Some(by)
                } else {
                    None
                }
            }
            Err(_) => None,
        }
    }
}

struct CmdStat {
    name: String,
    pid: i32,
    pss: f32,
    shared: f32,
    swap: f32,
    count: i32, // used when summing up
}

impl CmdStat {
    fn try_new(pid: i32) -> Result<Self> {
        let name = proc_name(pid)?;

        let mut stats = CmdStat {
            name,
            pid,
            pss: 0.0,
            shared: 0.0,
            swap: 0.0,
            count: 1,
        };

        // try to use rollups first, fall back to non-rollups otherwise
        if stats.collect_memory_usage(false).is_err() {
            stats.collect_memory_usage(false)?;
        }

        Ok(stats)
    }

    fn collect_memory_usage(&mut self, use_rollup: bool) -> Result<()> {
        const TY_PSS: &[u8] = b"Pss:";
        const TY_SWAP: &[u8] = b"Swap:";
        const TY_PRIVATE_CLEAN: &[u8] = b"Private_Clean:";
        const TY_PRIVATE_DIRTY: &[u8] = b"Private_Dirty:";

        let rollup_suffix = if use_rollup { "_rollup" } else { "" };
        let path = format!("/proc/{}/smaps{}", self.pid, rollup_suffix);
        let file = File::open(path)?;

        let pss_adjust = if use_rollup { 0.5 } else { 0.0 };

        let mut private: f32 = 0.0;
        let mut buffer = vec![];

        for line in Lines::new(file, &mut buffer) {
            if line.starts_with(TY_PSS) {
                if let Ok(n) = parse_line(&line) {
                    self.pss += n + pss_adjust;
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

fn parse_line(line: &[u8]) -> Result<f32> {
    use std::str;
    let kbs = unsafe { str::from_utf8_unchecked(&line[16..24]) };
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

fn is_digit(d: char) -> bool {
    d >= '0' && d <= '9'
}

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
    let dir = fs::read_dir("/proc")?;

    // TODO: look at filter_map
    let pids: Vec<i32> = dir
        .filter(|e| e.is_ok()) // ignore bad dirent results
        .map(|e| e.unwrap()) // make this a list of dirents
        .filter(|e| is_digit(first_char(&e.file_name())))
        .filter(|e| fs::metadata(e.path()).and_then(is_dir).is_ok())
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
        .filter_map(|pid| CmdStat::try_new(pid).ok())
        .collect()
}

fn main() {
    // TODO: command line flag parsing

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

    for cmd in &stats {
        let swap = if cmd.swap > 0.0 {
            format!("{:10.1}", cmd.swap)
        } else {
            "".to_string()
        };
        println!(
            "{:10.1}{:10.1}{:10}\t{} ({})",
            cmd.pss / 1024.0,
            cmd.shared / 1024.0,
            swap,
            cmd.name,
            cmd.count
        )
    }

    let total_pss = stats.iter().fold(0.0, |sum, stat| sum + stat.pss);
    let total_swap = stats.iter().fold(0.0, |sum, stat| sum + stat.swap);

    println!(
        "#{:9.1}{:20.1}\tTOTAL USED BY PROCESSES",
        total_pss / 1024.0,
        total_swap / 1024.0
    );
}
