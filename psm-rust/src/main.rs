// Copyright 2015 Bobby Powers. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#![feature(convert,str_char)]

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

fn is_digit(d: char) -> bool {
    d >= '0' && d <= '9'
}

fn not_dir<T>() -> Result<T, Error> {
    Err(Error::new(ErrorKind::NotFound, "not a directory"))
}

fn get_pids<'a>() -> Result<Vec<i32>, String> {
    let mut dir = fs::read_dir(PROC_PATH);
    if dir.is_err() {
	return Err(format!("read_dir({}): {}", PROC_PATH, dir.err().unwrap()));
    }

    // TODO: look at filter_map
    let pids: Vec<i32> = dir.unwrap()
	.filter(|e| e.is_ok()) // ignore bad dirent results
	.map(|e| e.unwrap())   // make this a list of dirents
	.filter(|e| is_digit(e.file_name().to_str().unwrap().char_at(0)))
	.filter(|e| fs::metadata(e.path()).and_then(|md| if md.is_dir() { Ok("") } else { not_dir() }).is_ok())
	.map(|e| i32::from_str_radix(e.file_name().to_str().unwrap(), 10).unwrap())
	.collect();

    Ok(pids)
}
/*
    {
	Err(err) => println!("read_dir({}): {}", PROC_PATH, err),
	Ok(result) => {
	    // result.filter()
	}
	    for entry in result {
	    match entry {
		Err(_) => (), // ignore
		Ok(dent) => {
		    if !is_digit(dent.file_name().to_str().unwrap().char_at(0)) {
			continue;
		    }
		    match fs::metadata(dent.path()) {
			Err(_) => (), // ignore
			Ok(md) => {
			    if md.is_dir() {
				pids.push(i32::from_str_radix(dent.file_name().to_str().unwrap(), 10).unwrap());
			    }
			}
		    }
		},
	    }
	},
    };

    return pids;
}
*/

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
    if pids_r.is_err() {
	println!("get_pids: {}", pids_r.err().unwrap());
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
