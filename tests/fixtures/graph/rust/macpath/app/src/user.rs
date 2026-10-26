//! Imports the re-exported macro by path.
use crate::emit::two;
use crate::helpers::{one, twice};
use crate::{emit, items};

pub fn run() {
    let pair = twice!(one());
    println!("{:?}", pair);
    let mut out: Vec<u32> = Vec::new();
    emit!(out, [two(), 3]);
    println!("{:?} {:?}", out, items![4]);
}
