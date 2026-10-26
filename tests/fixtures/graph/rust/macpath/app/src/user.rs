//! Imports the re-exported macro by path.
use crate::helpers::{one, twice};

pub fn run() {
    let pair = twice!(one());
    println!("{:?}", pair);
}
