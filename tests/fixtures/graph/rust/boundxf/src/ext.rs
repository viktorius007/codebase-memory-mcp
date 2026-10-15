pub trait Bound {}
pub trait Ext { fn go(self); }
impl<T: Bound> Ext for T { fn go(self) { println!("Ext::go blanket\n{}", std::backtrace::Backtrace::force_capture()); } }
pub trait Ext2 { fn go(&self); }
