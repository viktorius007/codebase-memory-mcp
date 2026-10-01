pub struct S { pub a: u8, pub b: u8 }
pub fn f(s: S) -> u8 { let S { #[cfg(all())] a, .. } = s; match s { S { #[cfg(all())] b, .. } => a + b } }
