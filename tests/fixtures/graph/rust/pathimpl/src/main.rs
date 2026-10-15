pub struct S;
pub mod a {
    pub trait Tr { fn go(&self); }
    impl Tr for super::S { fn go(&self) { println!("a::Tr::go\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tc { fn come(&self); }
    impl Tc for crate::S { fn come(&self) { println!("a::Tc::come\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn call(s: &super::S) { s.go(); s.come(); }
}
fn main() { a::call(&S); }
