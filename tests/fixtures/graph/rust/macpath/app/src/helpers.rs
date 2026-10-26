//! A module-local macro re-exported by `use`, as pm's replay_contract helpers do.

macro_rules! twice {
    ($e:expr) => {
        ($e, $e)
    };
}
pub(crate) use twice;

pub fn one() -> u32 { println!("one\n{}", std::backtrace::Backtrace::force_capture()); 1 }
