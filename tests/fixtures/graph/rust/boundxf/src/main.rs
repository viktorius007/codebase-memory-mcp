mod ext;
use ext::{Bound, Ext, Ext2};
pub struct W<T>(T);
impl Bound for W<u8> {}
impl Ext2 for W<u16> { fn go(&self) { println!("Ext2::go W<u16>\n{}", std::backtrace::Backtrace::force_capture()); } }
pub struct A;
impl Bound for A {}
fn call(w: W<u16>) { w.go(); }
fn call8(w: W<u8>) { w.go(); }
fn calla(a: A) { a.go(); }
fn main() { call(W(1u16)); call8(W(1u8)); calla(A); }
