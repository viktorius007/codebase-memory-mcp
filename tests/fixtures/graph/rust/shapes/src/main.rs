// inherent go(&self) on each type; trait go(self) impl written in various spellings. rustc picks trait (rank 0).
pub trait Tr { fn go(self); }
pub struct S1; pub struct S2; pub struct S3; pub struct G<T>(T); pub struct S5;
impl S1 { fn go(&self) { println!("S1 inherent\n{}", std::backtrace::Backtrace::force_capture()); } }
impl S2 { fn go(&self) { println!("S2 inherent\n{}", std::backtrace::Backtrace::force_capture()); } }
impl S3 { fn go(&self) { println!("S3 inherent\n{}", std::backtrace::Backtrace::force_capture()); } }
impl<T> G<T> { fn go(&self) { println!("G inherent\n{}", std::backtrace::Backtrace::force_capture()); } }
impl S5 { fn go(&self) { println!("S5 inherent\n{}", std::backtrace::Backtrace::force_capture()); } }
type A1 = S1;
impl Tr for A1 { fn go(self) { println!("S1 Tr via alias\n{}", std::backtrace::Backtrace::force_capture()); } }
impl Tr for crate::S2 { fn go(self) { println!("S2 Tr via crate path\n{}", std::backtrace::Backtrace::force_capture()); } }
impl Tr for self::S3 { fn go(self) { println!("S3 Tr via self path\n{}", std::backtrace::Backtrace::force_capture()); } }
impl Tr for G<u8> { fn go(self) { println!("G<u8> Tr\n{}", std::backtrace::Backtrace::force_capture()); } }
mod inner { impl super::Tr for super::S5 { fn go(self) { println!("S5 Tr from inner mod\n{}", std::backtrace::Backtrace::force_capture()); } } }
fn c1(s: S1) { s.go(); }
fn c2(s: S2) { s.go(); }
fn c3(s: S3) { s.go(); }
fn c4(g: G<u8>) { g.go(); }
fn c4b(g: G<u16>) { g.go(); }
fn c5(s: S5) { s.go(); }
fn main() { c1(S1); c2(S2); c3(S3); c4(G(1)); c4b(G(1)); c5(S5); }
