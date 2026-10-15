trait Bound {}
struct W<T>(T);
impl Bound for W<u8> {}
trait Ext { fn go(self); }
impl<T: Bound> Ext for T { fn go(self) { println!("Ext::go blanket\n{}", std::backtrace::Backtrace::force_capture()); } }
trait Ext2 { fn go(&self); }
impl Ext2 for W<u16> { fn go(&self) { println!("Ext2::go W<u16>\n{}", std::backtrace::Backtrace::force_capture()); } }
fn call(w: W<u16>) { w.go(); }
fn call8(w: W<u8>) { w.go(); }
fn main() { call(W(1u16)); call8(W(1u8)); }
