//! Corpus macros reached only through glob imports: a prelude glob chain
//! across crates, and a child module's `use super::*`.
use facade::prelude::*;

mod beside_std_glob;
mod checks;

fn one() -> u32 {
    println!("one\n{}", std::backtrace::Backtrace::force_capture());
    1
}

fn two() -> u32 {
    println!("two\n{}", std::backtrace::Backtrace::force_capture());
    2
}

fn spawn_two() {
    let (Spawn(a), Spawn(b)) = children![one(), two()];
    assert_eq!(a + b, 3);
}

fn main() {
    spawn_two();
    checks::run();
    beside_std_glob::run();
}
