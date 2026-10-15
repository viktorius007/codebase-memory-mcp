use std::ops::{Add, Index, Sub};
#[derive(Clone, Copy)] pub struct V;
#[derive(Clone, Copy)] pub struct W;
impl Add<V> for V { type Output = V; fn add(self, _o: V) -> V { println!("Add<V> for V\n{}", std::backtrace::Backtrace::force_capture()); self } }
impl Add<u8> for V { type Output = V; fn add(self, _o: u8) -> V { println!("Add<u8> for V\n{}", std::backtrace::Backtrace::force_capture()); self } }
impl Sub<W> for W { type Output = W; fn sub(self, _o: W) -> W { println!("Sub for W\n{}", std::backtrace::Backtrace::force_capture()); self } }
impl Index<usize> for V { type Output = u8; fn index(&self, _i: usize) -> &u8 { println!("Index<usize>\n{}", std::backtrace::Backtrace::force_capture()); &0 } }
impl Index<&str> for V { type Output = u8; fn index(&self, _i: &str) -> &u8 { println!("Index<&str>\n{}", std::backtrace::Backtrace::force_capture()); &1 } }
impl Index<usize> for W { type Output = u8; fn index(&self, _i: usize) -> &u8 { println!("Index<usize> W\n{}", std::backtrace::Backtrace::force_capture()); &0 } }
pub fn add_v(a: V, b: V) { let _ = a + b; }
pub fn add_u8(a: V) { let _ = a + 1u8; }
pub fn sub_w(a: W, b: W) { let _ = a - b; }
pub fn idx_usize(a: V) { let _ = a[0]; }
pub fn idx_w(a: W) { let _ = a[0]; }
fn main() { add_v(V, V); add_u8(V); sub_w(W, W); idx_usize(V); idx_w(W); }
