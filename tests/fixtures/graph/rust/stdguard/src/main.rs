use std::collections::HashMap;
use std::mem::take;
use std::ops::Index;
#[derive(Default, Clone)]
pub struct Foo(u8);
impl Foo { pub fn m(&self) -> u8 { println!("Foo::m\n{}", std::backtrace::Backtrace::force_capture()); self.0 } }
pub struct Other(u8);
impl Other { pub fn m(&self) -> u8 { println!("Other::m\n{}", std::backtrace::Backtrace::force_capture()); self.0 } }
pub trait Ent { fn entry(&self, k: u8) -> Other; }
impl Ent for HashMap<u8, Foo> { fn entry(&self, k: u8) -> Other { Other(k) } }
pub trait GetMut { fn get_mut(&self, i: usize) -> Option<Other>; }
impl GetMut for Vec<Foo> { fn get_mut(&self, i: usize) -> Option<Other> { Some(Other(i as u8)) } }
pub struct Bag { v: Vec<Foo> }
impl Index<usize> for Bag { type Output = Foo; fn index(&self, i: usize) -> &Foo { &self.v[i] } }
impl<'a> Index<usize> for &'a Bag { type Output = Other; fn index(&self, _i: usize) -> &Other { &OTHER } }
static OTHER: Other = Other(7);
fn g_trait_entry(m: &HashMap<u8, Foo>) { m.entry(0).m(); }
fn g_trait_get_mut(v: &Vec<Foo>) { v.get_mut(0).unwrap().m(); }
fn g_ref_index(b: &Bag) { b[0].m(); }
fn g_shadow_take() { let take: fn(&mut Foo) -> Other = |_f| Other(1); let mut f = Foo(1); take(&mut f).m(); }
fn g_std_take(f: &mut Foo) { take(f).m(); }
fn g_range_var(v: &mut [Foo]) { let r = 0..1; v.get_mut(r).unwrap()[0].m(); }
fn main() {
    let mut m = HashMap::new(); m.insert(0u8, Foo(1));
    g_trait_entry(&m); g_trait_get_mut(&vec![Foo(1)]); g_ref_index(&Bag { v: vec![Foo(1)] });
    g_shadow_take(); g_std_take(&mut Foo(2)); g_range_var(&mut [Foo(1)]);
    println!("end\n{}", std::backtrace::Backtrace::force_capture());
}
