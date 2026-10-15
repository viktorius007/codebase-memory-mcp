pub struct S;
impl S { pub fn m(&self) { println!("S::m inherent (&self)\n{}", std::backtrace::Backtrace::force_capture()); } }
pub trait Tr { fn m(&self); }
impl Tr for Box<S> { fn m(&self) { println!("<Box<S> as Tr>::m (&self)\n{}", std::backtrace::Backtrace::force_capture()); } }
pub fn by_boxbox(v: Box<Box<S>>) { v.m(); }
pub fn by_box(v: Box<S>) { v.m(); }
pub fn by_refbox(v: &Box<S>) { v.m(); }
pub fn by_mutrefbox(v: &mut Box<S>) { v.m(); }

pub struct U;
pub trait Ta { fn n(&self); }
impl Ta for U { fn n(&self) { println!("<U as Ta>::n (&self)\n{}", std::backtrace::Backtrace::force_capture()); } }
pub trait Tb { fn n(&self); }
impl Tb for Box<U> { fn n(&self) { println!("<Box<U> as Tb>::n (&self)\n{}", std::backtrace::Backtrace::force_capture()); } }
pub fn two_traits(v: Box<Box<U>>) { v.n(); }
fn main() { by_box(Box::new(S)); by_boxbox(Box::new(Box::new(S))); by_refbox(&Box::new(S)); by_mutrefbox(&mut Box::new(S)); two_traits(Box::new(Box::new(U))); }
