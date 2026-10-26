//! The parent's `use` of a macro, seen by children through `use super::*`.
use chk::check;

mod qualified;
mod via_glob;

pub fn truthy() -> bool {
    println!("truthy\n{}", std::backtrace::Backtrace::force_capture());
    true
}

pub fn run() {
    via_glob::run();
    qualified::run();
}
