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

/// A block `use` shadows the file's `twice`: the name means std's
/// stringify! here, so `one()` is never called.
pub fn shadowed() {
    use std::stringify as twice;
    println!("{}", twice!(one()));
}
