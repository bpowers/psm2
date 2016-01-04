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

fn is_digit(d: u8) -> bool {
    d >= ('0' as u8) && d <= ('9' as u8)
}

fn main() {

    // command line flag parsing

    // unsafe {
    // 	let uid = geteuid();
    // 	if uid != 0 {
    // 	    println!("we aren't root");
    // 	}
    // }

    let mut pids: Vec<i32> = vec!();

    // get a list of all pids
    /*let result =*/ match fs::read_dir(PROC_PATH) {
	Err(err) => {
	    println!("read_dir({}): {}", PROC_PATH, err);
	    return;
	},
	Ok(result) => {
	    for entry in result {
		entry.map(|dent| {
		    fs::metadata(dent.path()).map(|md| {
			if md.is_dir() && is_digit(dent.file_name().to_bytes().unwrap()[0]) {
			    let file_name = dent.file_name();
			    let name = file_name.to_str().unwrap();
			    println!("pid: {}", name);
			}
		    });
		})
	    };
	},
    };

    // get details of all pids (possibly in parallel)

    // sort pid details

    // find total

    // print_results

    println!("Hello, world!");
}
