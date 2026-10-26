#[derive(Clone, Copy)]
pub struct Srgba(pub f32);
#[derive(Clone, Copy)]
pub struct Hsla(pub f32);
pub struct LinearRgba(pub f32);

impl Srgba {
    pub fn lighter(self, by: f32) -> Srgba {
        println!("Srgba::lighter\n{}", std::backtrace::Backtrace::force_capture());
        Srgba(self.0 + by)
    }
}

impl From<Srgba> for LinearRgba {
    fn from(c: Srgba) -> Self {
        println!("LinearRgba::from<Srgba>\n{}", std::backtrace::Backtrace::force_capture());
        LinearRgba(c.0)
    }
}

impl From<Hsla> for LinearRgba {
    fn from(c: Hsla) -> Self {
        LinearRgba(c.0)
    }
}

pub mod palettes {
    pub mod css {
        use crate::color::Srgba;
        pub const ANTIQUE_WHITE: Srgba = Srgba(0.9);
        pub static ORANGE_RED: Srgba = Srgba(0.5);
    }
}
