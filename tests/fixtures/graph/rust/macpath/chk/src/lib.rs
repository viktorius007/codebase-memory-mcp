//! A dependency crate exporting assertion macros, as pm's pm-check does.

#[macro_export]
macro_rules! check_eq {
    ($left:expr, $right:expr, $msg:literal) => {
        match (&$left, &$right) {
            (left, right) => {
                if !(*left == *right) {
                    ::core::panic!("{}", $msg)
                }
            }
        }
    };
}

#[macro_export]
macro_rules! check {
    ($cond:expr, $msg:literal) => {
        if !$cond {
            ::core::panic!("{}", $msg)
        }
    };
}

#[macro_export]
macro_rules! check_both {
    ($a:expr, $b:expr) => {{
        $crate::check!($a, "first holds");
        $crate::check!($b, "second holds");
    }};
}
