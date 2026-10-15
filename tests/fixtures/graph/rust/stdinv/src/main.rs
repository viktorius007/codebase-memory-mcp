use std::collections::HashMap;
pub struct S;
pub trait Frob { fn frob(&self) -> u8; }
impl Frob for Vec<u8> { fn frob(&self) -> u8 { println!("Frob for Vec\n{}", std::backtrace::Backtrace::force_capture()); 1 } }
impl Frob for str { fn frob(&self) -> u8 { println!("Frob for str\n{}", std::backtrace::Backtrace::force_capture()); 2 } }
impl Frob for u32 { fn frob(&self) -> u8 { println!("Frob for u32\n{}", std::backtrace::Backtrace::force_capture()); 3 } }
impl Frob for Option<S> { fn frob(&self) -> u8 { println!("Frob for Option<S>\n{}", std::backtrace::Backtrace::force_capture()); 4 } }
impl Frob for HashMap<u8, u8> { fn frob(&self) -> u8 { println!("Frob for HashMap\n{}", std::backtrace::Backtrace::force_capture()); 5 } }
impl Frob for char { fn frob(&self) -> u8 { println!("Frob for char\n{}", std::backtrace::Backtrace::force_capture()); 6 } }
impl Frob for S { fn frob(&self) -> u8 { println!("Frob for S\n{}", std::backtrace::Backtrace::force_capture()); 7 } }
impl Frob for [u8] { fn frob(&self) -> u8 { println!("Frob for [u8]\n{}", std::backtrace::Backtrace::force_capture()); 8 } }
pub trait MyMap { fn map<F: FnMut(u8) -> u8>(self, f: F) -> [u8; 3]; }
impl MyMap for [u8; 3] { fn map<F: FnMut(u8) -> u8>(self, f: F) -> [u8; 3] { println!("MyMap for array\n{}", std::backtrace::Backtrace::force_capture()); self } }
pub trait MyLen { fn len(self) -> usize; }
impl MyLen for [u8; 3] { fn len(self) -> usize { println!("MyLen for array\n{}", std::backtrace::Backtrace::force_capture()); 0 } }
pub fn vec_frob(v: Vec<u8>) { v.frob(); }
pub fn string_frob(s: String) { s.frob(); }
pub fn str_frob(s: &str) { s.frob(); }
pub fn u32_frob(n: u32) { n.frob(); }
pub fn opt_frob(o: Option<S>) { o.frob(); }
pub fn map_frob(m: HashMap<u8, u8>) { m.frob(); }
pub fn char_frob(c: char) { c.frob(); }
pub fn box_frob(b: Box<S>) { b.frob(); }
pub fn rc_vec_frob(r: std::rc::Rc<Vec<u8>>) { r.frob(); }
pub fn slice_frob(s: &[u8]) { s.frob(); }
pub fn array_map(a: [u8; 3]) { let _ = a.map(|x| x + 1); }
pub fn array_len(a: [u8; 3]) { let _ = a.len(); }
fn main() {
    vec_frob(vec![]); string_frob(String::new()); str_frob(""); u32_frob(1); opt_frob(None);
    map_frob(HashMap::new()); char_frob('c'); box_frob(Box::new(S)); rc_vec_frob(std::rc::Rc::new(vec![]));
    slice_frob(&[1]); array_map([1, 2, 3]); array_len([1, 2, 3]);
}
