use std::ops::Deref;
pub struct W(Vec<u8>);
impl Deref for W { type Target = Vec<u8>; fn deref(&self) -> &Vec<u8> { &self.0 } }
pub trait Ext { fn first(&self) -> Option<&u8>; fn frob(&self) -> u8; fn len(&self) -> usize; }
impl Ext for [u8] {
    fn first(&self) -> Option<&u8> { println!("Ext::first for [u8]\n{}", std::backtrace::Backtrace::force_capture()); None }
    fn frob(&self) -> u8 { println!("Ext::frob for [u8]\n{}", std::backtrace::Backtrace::force_capture()); 0 }
    fn len(&self) -> usize { println!("Ext::len for [u8]\n{}", std::backtrace::Backtrace::force_capture()); 0 }
}
pub struct Bx(Box<[u8]>);
impl Deref for Bx { type Target = Box<[u8]>; fn deref(&self) -> &Box<[u8]> { &self.0 } }
pub fn w_first(w: &W) { let _ = w.first(); }
pub fn w_frob(w: &W) { let _ = w.frob(); }
pub fn w_len(w: &W) { let _ = w.len(); }
pub fn v_first(v: &Vec<u8>) { let _ = v.first(); }
pub fn bx_first(b: &Bx) { let _ = b.first(); }
pub fn bx_frob(b: &Bx) { let _ = b.frob(); }
pub fn s_len(s: &[u8]) { let _ = s.len(); }
fn main() { let w = W(vec![1]); w_first(&w); w_frob(&w); w_len(&w); v_first(&vec![]); let b = Bx(Box::new([1u8])); bx_first(&b); bx_frob(&b); s_len(&[1]); println!("end\n{}", std::backtrace::Backtrace::force_capture()); }
