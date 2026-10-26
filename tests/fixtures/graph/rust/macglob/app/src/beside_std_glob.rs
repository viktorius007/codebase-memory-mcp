//! A dependency's macro by path beside a glob of an external module: the
//! glob cannot shadow the path head (E0659), so `chk` is the dependency.
use std::collections::*;

fn listed() -> bool {
    println!("listed\n{}", std::backtrace::Backtrace::force_capture());
    true
}

pub fn run() {
    let set: BTreeSet<u8> = BTreeSet::new();
    chk::check!(listed() && set.is_empty(), "dependency path beside an external glob");
}
