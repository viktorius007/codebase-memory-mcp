pub trait Tr { const N: usize; type A<'a> where Self: 'a; fn f(&self) -> impl Iterator<Item = u8> + use<'_, Self>; }
