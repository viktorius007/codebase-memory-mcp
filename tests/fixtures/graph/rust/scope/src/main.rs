pub struct S;
pub mod a { pub trait Tr { fn go(&self); }
 use super::S; impl Tr for S { fn go(&self) { println!("a::Tr::go\n{}", std::backtrace::Backtrace::force_capture()); } } }
pub mod b { pub trait Tr2 { fn go(&self); }
 use super::S; impl Tr2 for S { fn go(&self) { println!("b::Tr2::go\n{}", std::backtrace::Backtrace::force_capture()); } } }
pub mod x { use crate::a::Tr; pub fn call(s: &crate::S) { s.go(); } }
pub mod y { use crate::b::Tr2 as _; pub fn call(s: &crate::S) { s.go(); } }
pub mod z {
    pub fn block_a(s: &crate::S) { use crate::a::Tr; s.go(); }
    pub fn block_b(s: &crate::S) { use crate::b::*; s.go(); }
}
pub mod w {
    use crate::b::Tr2;
    pub fn outer(s: &crate::S) { s.go(); }
    #[cfg(test)] mod t {}
    pub mod inner { use super::*; pub fn call(s: &crate::S) { s.go(); } }
}
// blanket applicability: Ext applies only to Copy types; S is not Copy
pub mod c { use crate::S;
    pub trait Ext { fn hop(&self); }
    impl<T: Copy> Ext for T { fn hop(&self) { println!("c::Ext::hop (blanket Copy)\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Ext2 { fn hop(&self); }
    impl Ext2 for S { fn hop(&self) { println!("c::Ext2::hop (S)\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn call(s: &crate::S) { s.hop(); }
    pub fn call_copy(v: &u8) { v.hop(); }
}
fn main() {
    let s = S;
    x::call(&s); y::call(&s); z::block_a(&s); z::block_b(&s); w::outer(&s); w::inner::call(&s);
    c::call(&s); c::call_copy(&1u8);
}
