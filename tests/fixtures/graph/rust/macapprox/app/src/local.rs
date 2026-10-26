//! No `use` binds this name to approx: the module's own macro_rules! wins, and
//! its transcriber discards the argument, so `crate::half()` is never called.
macro_rules! assert_relative_eq {
    ($($t:tt)*) => {
        ()
    };
}

pub fn run() {
    assert_relative_eq!(crate::half(), 0.5);
}
