//! A module-local macro re-exported by `use`, as pm's replay_contract helpers do.

macro_rules! twice {
    ($e:expr) => {
        ($e, $e)
    };
}
pub(crate) use twice;

pub fn one() -> u32 { println!("one\n{}", std::backtrace::Backtrace::force_capture()); 1 }

// `use once;` re-exports the definition visible where it stands; the later
// definition shadows it only for text after it.
macro_rules! once {
    ($e:expr) => {
        stringify!($e)
    };
}
pub(crate) use once;
#[allow(unused_macros)]
macro_rules! once {
    ($e:expr) => {
        $e
    };
}
