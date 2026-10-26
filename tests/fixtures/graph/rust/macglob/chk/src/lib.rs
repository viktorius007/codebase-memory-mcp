//! An assertion macro without `$crate`.
#[macro_export]
macro_rules! check {
    ($cond:expr, $msg:literal) => {
        if !$cond {
            ::core::panic!($msg)
        }
    };
}
