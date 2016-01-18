// Copyright 2015 Bobby Powers. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#![feature(convert)]

//extern crate libc;
//use libc::geteuid;

use std::fs;

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

fn get_pids() -> Vec<i32> {
    let mut pids: Vec<i32> = vec!();

    match fs::read_dir(PROC_PATH) {
	Err(err) => println!("read_dir({}): {}", PROC_PATH, err),
	Ok(result) => for entry in result {
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

    let pids = get_pids();

    // get details of all pids (possibly in parallel)
    let infos = cmdinfos_for(pids);

    // sort pid details

    // find total

    // print_results

    println!("Hello, world!");
}
