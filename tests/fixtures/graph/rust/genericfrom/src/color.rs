#[derive(Clone, Copy)]
pub struct Color(pub f32);
#[derive(Clone, Copy)]
pub struct Srgba(pub f32);

impl From<Srgba> for Color {
    fn from(c: Srgba) -> Self {
        Color(c.0)
    }
}

pub struct TextColor(pub Color);

impl<T: Into<Color>> From<T> for TextColor {
    fn from(color: T) -> Self {
        println!("TextColor::from\n{}", std::backtrace::Backtrace::force_capture());
        TextColor(color.into())
    }
}

pub const YELLOW: Srgba = Srgba(0.5);
