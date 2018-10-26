// Copyright 2015 Bobby Powers. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

//extern crate libc;
//use libc::geteuid;

use std::fs::{self, File};
use std::io::{Error, ErrorKind, Read, Result};

const PROC_PATH: &'static str = "/proc";

struct CmdStat {
    name: String,
    pss: f32,
    shared: f32,
    heap: f32,
    swap: f32,
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

fn cmdstat_new(pid: i32) -> Option<CmdStat> {
    match proc_name(pid) {
        Ok(name) => Some(CmdStat {
            name: name,
            pss: 0.0,
            shared: 0.0,
            heap: 0.0,
            swap: 0.0,
        }),
        Err(_) => None,
    }
}

fn cmdstats_for(pids: Vec<i32>) -> Vec<CmdStat> {
    pids.into_iter().filter_map(cmdstat_new).collect()
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
    // get details of all pids (possibly in parallel)
    let infos = cmdstats_for(pids);

    // sort pid details

    // find total

    // print_results
}
