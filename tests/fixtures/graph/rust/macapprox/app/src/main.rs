//! Calls written inside the arguments of approx's assertion macros resolve as the
//! same expressions do outside them.
use approx::assert_abs_diff_eq;

mod local;

#[derive(Clone, Copy)]
struct Iso(f32);

impl Iso {
    fn turn(self) -> f32 { println!("turn\n{}", std::backtrace::Backtrace::force_capture()); self.0 }
}

impl core::ops::Mul for Iso {
    type Output = f32;
    fn mul(self, rhs: Iso) -> f32 { println!("mul\n{}", std::backtrace::Backtrace::force_capture()); self.0 * rhs.0 }
}

fn half() -> f32 { println!("half\n{}", std::backtrace::Backtrace::force_capture()); 0.5 }
fn tolerance() -> f32 { println!("tolerance\n{}", std::backtrace::Backtrace::force_capture()); 1e-3 }

fn imported() {
    let iso = Iso(0.5);
    assert_abs_diff_eq!(iso.turn(), half());
    assert_abs_diff_eq!(iso * Iso(1.0), 0.5, epsilon = tolerance());
}

fn qualified() {
    approx::assert_relative_eq!(Iso(0.5).turn(), half(), epsilon = 1e-4);
}

mod tests_like {
    use approx::{assert_relative_eq, AbsDiffEq};

    pub fn nested() {
        let _ = 1.0_f32.abs_diff_eq(&1.0, 0.1);
        assert_relative_eq!(super::Iso(1.0) * super::Iso(0.5), super::half());
    }
}

fn main() {
    imported();
    qualified();
    tests_like::nested();
    local::run();
}
