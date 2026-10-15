pub mod one {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(self) { println!("one inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&self); }
    impl Tr for S { fn m(&self) { println!("one tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &S) { v.m(); }
}
pub mod two {
    #[derive(Clone, Copy)] pub struct S;
    pub trait Tr { fn m(&self); }
    impl Tr for S { fn m(&self) { println!("two tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &S) { v.m(); }
}
fn main() { one::probe(&one::S); two::probe(&two::S); }
