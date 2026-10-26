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

/// Selects its arm by the string it is given.
#[macro_export]
macro_rules! tagged {
    ("chk", $e:expr) => {
        $e
    };
    ($t:literal, $e:expr) => {
        stringify!($e)
    };
}

/// Passes the string "$crate" itself: `$crate` inside a literal is text.
#[macro_export]
macro_rules! via_tag {
    ($e:expr) => {
        $crate::tagged!("$crate", $e)
    };
}
