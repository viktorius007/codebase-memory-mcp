pub struct L { pub a: u8 }
pub enum E { V(L) }
pub fn f(l: L, e: E) -> u8 { let crate::L { a } = l; match e { E::V(crate::L { a: b }) => a + b } }
