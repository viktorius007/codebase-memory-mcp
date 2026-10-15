trait Bound {}
struct A; struct B;
impl Bound for A {}
trait Ext { fn go(self); }
impl<T> Ext for T where T: Bound { fn go(self) { println!("Ext::go where-bound\n{}", std::backtrace::Backtrace::force_capture()); } }
trait Ext2 { fn go(&self); }
impl Ext2 for B { fn go(&self) { println!("Ext2::go B\n{}", std::backtrace::Backtrace::force_capture()); } }
fn ca(a: A) { a.go(); }
fn cb(b: B) { b.go(); }
fn main() { ca(A); cb(B); }
