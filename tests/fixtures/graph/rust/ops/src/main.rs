use std::ops::{Add, Index};
#[derive(Clone, Copy)] pub struct V;
impl Add<V> for V { type Output = V; fn add(self, _o: V) -> V { println!("Add<V> for V\n{}", std::backtrace::Backtrace::force_capture()); self } }
impl Add<u8> for V { type Output = V; fn add(self, _o: u8) -> V { println!("Add<u8> for V\n{}", std::backtrace::Backtrace::force_capture()); self } }
impl Index<usize> for V { type Output = u8; fn index(&self, _i: usize) -> &u8 { println!("Index<usize>\n{}", std::backtrace::Backtrace::force_capture()); &0 } }
impl Index<&str> for V { type Output = u8; fn index(&self, _i: &str) -> &u8 { println!("Index<&str>\n{}", std::backtrace::Backtrace::force_capture()); &1 } }
pub trait Tr { fn go(&self); }
impl V {
    pub fn go(&self) { println!("V::go inherent\n{}", std::backtrace::Backtrace::force_capture()); }
    pub fn as_ref(&self) -> &V { println!("V::as_ref inherent\n{}", std::backtrace::Backtrace::force_capture()); self }
    pub fn is_null(&self) -> bool { println!("V::is_null inherent\n{}", std::backtrace::Backtrace::force_capture()); false }
    pub fn read(&self) -> V { println!("V::read inherent\n{}", std::backtrace::Backtrace::force_capture()); *self }
    pub fn cast(&self) -> u8 { println!("V::cast inherent\n{}", std::backtrace::Backtrace::force_capture()); 0 }
}
impl Tr for V { fn go(&self) { println!("<V as Tr>::go\n{}", std::backtrace::Backtrace::force_capture()); } }
pub fn add_v(a: V, b: V) { let _ = a + b; }
pub fn add_u8(a: V) { let _ = a + 1u8; }
pub fn idx_usize(a: V) { let _ = a[0]; }
pub fn idx_str(a: V) { let _ = a["k"]; }
pub fn ufcs_trait(a: V) { Tr::go(&a); }
pub fn ufcs_qualified(a: V) { <V as Tr>::go(&a); }
pub fn path_inherent(a: V) { V::go(&a); }
pub fn raw_as_ref(p: *const V) { let _ = unsafe { p.as_ref() }; }
pub fn raw_is_null(p: *const V) { let _ = p.is_null(); }
pub fn raw_read(p: *const V) { let _ = unsafe { p.read() }; }
pub fn raw_cast(p: *const V) { let _ = p.cast::<u8>(); }
pub fn raw_deref(p: *const V) { unsafe { (*p).go() }; }
fn main() {
    add_v(V, V); add_u8(V); idx_usize(V); idx_str(V); ufcs_trait(V); ufcs_qualified(V); path_inherent(V);
    let v = V; raw_as_ref(&v); raw_is_null(&v); raw_read(&v); raw_cast(&v); raw_deref(&v);
}
