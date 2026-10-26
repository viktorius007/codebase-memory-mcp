//! The approx 0.5 macro grammar: `lhs, rhs [, name = expression]*`.

pub trait AbsDiffEq<Rhs = Self> {
    fn abs_diff_eq(&self, other: &Rhs, epsilon: f32) -> bool;
}

impl AbsDiffEq for f32 {
    fn abs_diff_eq(&self, other: &f32, epsilon: f32) -> bool {
        (self - other).abs() <= epsilon
    }
}

#[macro_export]
macro_rules! assert_abs_diff_eq {
    ($given:expr, $expected:expr $(, $opt:ident = $val:expr)* $(,)?) => {{
        let mut epsilon = 1e-6_f32;
        $(epsilon = $val; let _ = stringify!($opt);)*
        assert!($crate::AbsDiffEq::abs_diff_eq(&$given, &$expected, epsilon));
    }};
}

#[macro_export]
macro_rules! assert_relative_eq {
    ($given:expr, $expected:expr $(, $opt:ident = $val:expr)* $(,)?) => {{
        let mut epsilon = 1e-6_f32;
        $(epsilon = $val; let _ = stringify!($opt);)*
        assert!($crate::AbsDiffEq::abs_diff_eq(&$given, &$expected, epsilon));
    }};
}
