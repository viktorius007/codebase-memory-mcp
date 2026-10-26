//! Exported macros of this crate reached by `use crate::..`, one invoking the
//! other through `$crate::`, as ruff_formatter's write! and format_args! do.

#[macro_export]
macro_rules! items {
    ($($v:expr),+ $(,)?) => {
        [$($v),+]
    };
}

#[macro_export]
macro_rules! emit {
    ($dst:expr, [$($a:expr),+ $(,)?]) => {
        $dst.extend($crate::items!($($a),+))
    };
}

pub fn two() -> u32 { println!("two\n{}", std::backtrace::Backtrace::force_capture()); 2 }
