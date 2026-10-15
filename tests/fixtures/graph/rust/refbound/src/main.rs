trait Bound {}
struct W;
impl Bound for W {}
trait Ext { fn go(self); }
impl<T: Bound> Ext for T { fn go(self) { println!("Ext::go blanket\n{}", std::backtrace::Backtrace::force_capture()); } }
trait Ext2 { fn go(&self); }
impl Ext2 for &W { fn go(&self) { println!("Ext2::go &W\n{}", std::backtrace::Backtrace::force_capture()); } }
fn call(r: &W) { r.go(); }
fn main() { call(&W); }
