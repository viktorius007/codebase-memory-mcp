#[allow(non_snake_case)] pub fn f32<T>(_: T) -> impl Fn(u8) -> u8 { |s| s }
pub fn g() -> u8 { let c = |s| f32::<u8>(1)(s); c(1) }
