pub struct S;
impl S { pub fn m(&self) { println!("S::m inherent (&self)\n{}", std::backtrace::Backtrace::force_capture()); } }
pub trait Tr { fn m(self); }
impl Tr for S { fn m(self) { println!("<S as Tr>::m (self)\n{}", std::backtrace::Backtrace::force_capture()); } }
pub fn by_value() { let v = S; v.m(); }

pub struct U;
impl U { pub fn n(&mut self) { println!("U::n inherent (&mut self)\n{}", std::backtrace::Backtrace::force_capture()); } }
pub trait Tn { fn n(&self); }
impl Tn for U { fn n(&self) { println!("<U as Tn>::n (&self)\n{}", std::backtrace::Backtrace::force_capture()); } }
pub fn by_boxed(mut b: Box<U>) { b.n(); }
fn main() { by_value(); by_boxed(Box::new(U)); }
