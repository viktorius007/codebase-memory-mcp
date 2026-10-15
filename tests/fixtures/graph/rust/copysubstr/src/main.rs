trait CopyLike {}
impl CopyLike for u16 {}
trait Ext { fn go(self); }
impl<T: CopyLike> Ext for T { fn go(self) { println!("Ext::go blanket CopyLike\n{}", std::backtrace::Backtrace::force_capture()); } }
trait Ext2 { fn go(&self); }
impl Ext2 for u8 { fn go(&self) { println!("Ext2::go u8\n{}", std::backtrace::Backtrace::force_capture()); } }
fn call(x: u8) { x.go(); }
fn call16(x: u16) { x.go(); }
fn main() { call(1); call16(1); }
