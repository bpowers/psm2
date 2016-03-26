// Copyright 2015 Bobby Powers. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#![feature(str_char)]

//extern crate libc;
//use libc::geteuid;

use std::fs;
use std::fs::{File};
use std::io::{Read, Error, ErrorKind};
use std::path::{PathBuf};

const PROC_PATH: &'static str = "/proc";

struct CmdInfo {
    name: &'static str,
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
fn first_char(name: std::ffi::OsString) -> char {
    name.to_str().unwrap().char_at(0)
}

fn is_dir(md: fs::Metadata) -> Result<fs::Metadata, Error> {
    if md.is_dir() {
	Ok(md)
    } else {
	Err(Error::new(ErrorKind::NotFound, "not a directory"))
    }
}

#[inline(never)]
fn get_pids() -> Result<Vec<i32>, String> {
    let dir = fs::read_dir(PROC_PATH);
    if let Err(err)= dir {
	return Err(format!("read_dir({}): {}", PROC_PATH, err));
    }

    // TODO: look at filter_map
    let pids: Vec<i32> = dir.unwrap()
	.filter(|e| e.is_ok()) // ignore bad dirent results
	.map(|e| e.unwrap())   // make this a list of dirents
	.filter(|e| is_digit(first_char(e.file_name())))
	.filter(|e| fs::metadata(e.path()).and_then(|md| is_dir(md)).is_ok())
	.map(|e| i32::from_str_radix(e.file_name().to_str().unwrap(), 10).unwrap())
	.collect();

    Ok(pids)
}

fn proc_cmdline(pid: i32) -> Result<Vec<u8>, String> {
    const BUFSIZ: usize = 1024;
    let mut buf = Vec::with_capacity(BUFSIZ);
    let path = format!("/proc/{}/cmdline", pid);

    // TODO: we don't really care about reading all of the
    // cmdline, just the first 1024 chars is enough.
    let mut f = match File::open(path) {
	Ok(f) => f,
	Err(_) => return Err("open failed".to_string())
    };


    if f.read_to_end(&mut buf).is_ok() {
	return Ok(buf)
    }

    Err("proc_cmdline failed".to_string())
}

fn proc_name(pid: i32) -> Result<PathBuf, String> {
    let path = format!("/proc/{}/exe", pid);

    if let Ok(full_path) = fs::read_link(path) {
	if let Ok(cmdline) = proc_cmdline(pid) {

	}
    }

    Err("proc_name failed".to_string())
}

fn cmdinfo_new(pid: i32) -> CmdInfo {

    let proc_name = proc_name(pid);

    return CmdInfo{
	name: "wah",
	pss: 0.0,
	shared: 0.0,
	heap: 0.0,
	swap: 0.0,
    }
}

fn cmdinfos_for(pids: Vec<i32>) -> Result<Vec<CmdInfo>, String> {

    Err("not implemented".to_string())
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
    let infos = cmdinfos_for(pids);

    // sort pid details

    // find total

    // print_results

    println!("Hello, world!");
}
