pub mod c404 {
    pub struct S404;
    impl S404 { pub fn m(self) { println!("c404 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr404 { fn m(&self); }
    impl Tr404 for Box<S404> { fn m(&self) { println!("c404 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &Box<S404>) { v.m(); }
    pub fn drive() { probe(&Box::new(S404)); }
}
pub mod c410 {
    pub struct S410;
    impl S410 { pub fn m(&self) { println!("c410 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr410 { fn m(&self); }
    impl Tr410 for Box<S410> { fn m(&self) { println!("c410 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &Box<S410>) { v.m(); }
    pub fn drive() { probe(&Box::new(S410)); }
}
fn main(){ c404::drive(); c410::drive(); }
