//! Calls written inside the arguments of corpus macros reached through path scope.
use chk::{check, check_both, check_eq};

mod emit;
mod helpers;
mod shadow;
mod user;

fn seven() -> u32 { println!("seven\n{}", std::backtrace::Backtrace::force_capture()); 7 }
fn truthy() -> bool { println!("truthy\n{}", std::backtrace::Backtrace::force_capture()); true }

fn direct() {
    check_eq!(seven(), 7, "seven is seven");
    check!(truthy(), "truthy holds");
}

fn literal_kept() {
    println!("{}", chk::via_tag!(truthy()));
}

fn nested() {
    check_both!(truthy(), seven() == 7);
}

fn qualified() {
    chk::check_eq!(seven(), 7, "qualified path");
}

fn main() {
    direct();
    qualified();
    nested();
    literal_kept();
    user::run();
    shadow::run();
    user::shadowed();
    user::reexported_first();
}
