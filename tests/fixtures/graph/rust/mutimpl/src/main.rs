pub struct S;
impl S { pub fn m(&mut self) { println!("S::m inherent (&mut self)\n{}", std::backtrace::Backtrace::force_capture()); } }
pub trait Tr { fn m(self); }
impl Tr for &mut S { fn m(self) { println!("<&mut S as Tr>::m (self)\n{}", std::backtrace::Backtrace::force_capture()); } }
pub fn probe(v: &mut S) { v.m(); }

pub struct U;
pub trait Fa { fn n(&mut self); }
impl Fa for U { fn n(&mut self) { println!("<U as Fa>::n (&mut self)\n{}", std::backtrace::Backtrace::force_capture()); } }
pub trait Fb { fn n(self); }
impl Fb for &U { fn n(self) { println!("<&U as Fb>::n (self)\n{}", std::backtrace::Backtrace::force_capture()); } }
pub fn probe_two_traits() { let mut u = U; u.n(); }
fn main() { probe(&mut S); probe_two_traits(); }
