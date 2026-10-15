trait Conv<X> {}
struct A;
impl Conv<u16> for A {}
trait Ext { fn go(self); }
impl<T: Conv<u8>> Ext for T { fn go(self) { println!("Ext::go blanket Conv<u8>\n{}", std::backtrace::Backtrace::force_capture()); } }
trait Ext2 { fn go(&self); }
impl Ext2 for A { fn go(&self) { println!("Ext2::go A\n{}", std::backtrace::Backtrace::force_capture()); } }
fn ca(a: A) { a.go(); }
fn main() { ca(A); }
