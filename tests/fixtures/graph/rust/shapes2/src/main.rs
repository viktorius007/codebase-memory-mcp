pub trait Tr { fn go(self); }
pub struct L<'a>(&'a u8);
pub struct D<T = u8>(T);
pub struct N;
pub mod m { pub struct S; }
impl<'a> L<'a> { fn go(&self) { println!("L inherent\n{}", std::backtrace::Backtrace::force_capture()); } }
impl D { fn go(&self) { println!("D inherent\n{}", std::backtrace::Backtrace::force_capture()); } }
impl N { fn go(&self) { println!("N inherent\n{}", std::backtrace::Backtrace::force_capture()); } }
impl m::S { fn go(&self) { println!("m::S inherent\n{}", std::backtrace::Backtrace::force_capture()); } }
impl<'a> Tr for L<'a> { fn go(self) { println!("L Tr\n{}", std::backtrace::Backtrace::force_capture()); } }
impl Tr for D<u8> { fn go(self) { println!("D<u8> Tr\n{}", std::backtrace::Backtrace::force_capture()); } }
impl Tr for (N) { fn go(self) { println!("N Tr paren\n{}", std::backtrace::Backtrace::force_capture()); } }
impl Tr for m::S { fn go(self) { println!("m::S Tr\n{}", std::backtrace::Backtrace::force_capture()); } }
type AD = D;
fn c1(l: L<'_>) { l.go(); }
fn c2(d: D) { d.go(); }
fn c3(n: N) { n.go(); }
fn c4(s: m::S) { s.go(); }
fn c5(d: AD) { d.go(); }
fn main() { c1(L(&1)); c2(D(1)); c3(N); c4(m::S); c5(D(1)); }
