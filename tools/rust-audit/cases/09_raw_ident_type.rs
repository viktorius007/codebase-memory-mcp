#[allow(non_camel_case_types)] pub struct raw {}
pub fn p<T>() {}
pub fn f() -> Option<raw> { p::<raw>(); None }
pub fn g(x: u8) -> u8 { match x { raw @ (1 | 2) => raw, _ => 0 } }
