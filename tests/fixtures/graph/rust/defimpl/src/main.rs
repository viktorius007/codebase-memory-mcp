pub struct S;
impl S { pub fn m(&self) { println!("S::m inherent (&self)\n{}", std::backtrace::Backtrace::force_capture()); } }
pub trait Tr { fn m(self) where Self: Sized { println!("<&S as Tr>::m default (self)\n{}", std::backtrace::Backtrace::force_capture()); } }
impl Tr for &S {}
pub fn owned(v: S) { v.m(); }

pub struct U;
impl U { pub fn n(&mut self) { println!("U::n inherent (&mut self)\n{}", std::backtrace::Backtrace::force_capture()); } }
pub trait Tn { fn n(&self) { println!("<&mut U as Tn>::n default (&self)\n{}", std::backtrace::Backtrace::force_capture()); } }
impl Tn for &mut U {}
pub fn owned_mut(mut v: U) { v.n(); }

fn main() { owned(S); owned_mut(U); }
