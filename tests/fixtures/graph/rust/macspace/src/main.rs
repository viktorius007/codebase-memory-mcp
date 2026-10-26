//! A matcher whose fragment specifiers are spaced from their names.

macro_rules! guard {
    ($expr: expr) => {
        match $expr {
            Ok(val) => val,
            Err(err) => return Err(err),
        }
    };
}

macro_rules! sum_all {
    ($( $item : expr ),* $(,)?) => {
        0 $(+ $item)*
    };
}

fn targets() -> Result<u32, String> { println!("targets\n{}", std::backtrace::Backtrace::force_capture()); Ok(2) }
fn fields() -> Result<u32, String> { println!("fields\n{}", std::backtrace::Backtrace::force_capture()); Ok(3) }
fn one() -> u32 { println!("one\n{}", std::backtrace::Backtrace::force_capture()); 1 }
fn two() -> u32 { println!("two\n{}", std::backtrace::Backtrace::force_capture()); 2 }

fn specialize() -> Result<u32, String> {
    let t = guard!(targets());
    let f = guard!(fields());
    Ok(t + f)
}

fn total() -> u32 {
    sum_all!(one(), two())
}

fn main() {
    let _ = specialize();
    let _ = total();
}
