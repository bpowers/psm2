// Copyright 2015 Bobby Powers. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#![feature(str_char)]

//extern crate libc;
//use libc::geteuid;

use std::fs;
use std::io::{Error, ErrorKind};

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
    let mut dir = fs::read_dir(PROC_PATH);
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

fn cmdinfos_for(pids: Vec<i32>) -> Vec<CmdInfo> {
    let mut infos: Vec<CmdInfo> = Vec::with_capacity(pids.len());

    for pid in pids {
	println!("pid: {}", pid);
    }

    return infos;
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
