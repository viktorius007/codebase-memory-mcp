mod color;

use color::TextColor;

fn yellow() -> f32 {
    TextColor::from(color::YELLOW).0 .0
}

fn same(text: TextColor) -> f32 {
    TextColor::from(text).0 .0
}

fn main() {
    yellow();
    same(TextColor(color::Color(1.0)));
    println!("end\n{}", std::backtrace::Backtrace::force_capture());
}
