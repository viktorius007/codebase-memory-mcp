//! A textual macro re-exported with `pub(super) use` whose transcriber assembles
//! `$receiver.$method(..)` from captured idents: the method call is the call site's.
use super::helpers::invoke;

pub struct Store;
impl Store {
    pub fn get(&self, k: u32) -> Option<u32> {
        println!("get\n{}", std::backtrace::Backtrace::force_capture());
        Some(k)
    }
}

fn key() -> u32 {
    println!("key\n{}", std::backtrace::Backtrace::force_capture());
    1
}

pub fn run() {
    let mut seen = Vec::new();
    let store = Store;
    let v = invoke!(seen, "Store", store.get(key()));
    assert_eq!(v, 1);
}
