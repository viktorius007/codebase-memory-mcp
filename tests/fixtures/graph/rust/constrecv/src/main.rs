mod color;

use color::palettes::css::*;
use color::LinearRgba;

fn scaled() -> f32 {
    LinearRgba::from(ANTIQUE_WHITE).0
}

fn accent() -> f32 {
    ORANGE_RED.lighter(0.2).0
}

fn main() {
    scaled();
    accent();
    println!("end\n{}", std::backtrace::Backtrace::force_capture());
}
