use std::collections::HashMap;
use std::ops::{Deref, DerefMut};
#[derive(Clone, Default, Debug, PartialEq)]
pub struct Foo(u8);
impl Foo {
    pub fn m(&self) -> u8 { println!("Foo::m\n{}", std::backtrace::Backtrace::force_capture()); self.0 }
}
#[derive(Clone, Copy, PartialEq, Eq, Hash, Debug)]
pub struct Id(u8);
#[derive(Default)]
pub struct Assets<A> { items: HashMap<u8, A>, list: Vec<A> }
impl<A> Assets<A> {
    pub fn get(&self, id: Id) -> Option<&A> { self.items.get(&id.0) }
    pub fn get_mut(&mut self, id: Id) -> Option<&mut A> { self.items.get_mut(&id.0) }
    pub fn iter(&self) -> impl Iterator<Item = (Id, &A)> { self.items.iter().map(|(k, v)| (Id(*k), v)) }
    pub fn values(&self) -> impl Iterator<Item = &A> + '_ { self.list.iter() }
    pub fn all(&self) -> &Vec<A> { &self.list }
    pub fn slice(&self) -> &[A] { &self.list }
    pub fn first(&self) -> Option<&A> { self.list.first() }
    pub fn owned(&self) -> Vec<A> where A: Clone { self.list.clone() }
    pub fn drained(&mut self) -> std::collections::hash_map::Drain<'_, u8, A> { self.items.drain() }
    pub fn pairs_iter(&self) -> std::collections::hash_map::Iter<'_, u8, A> { self.items.iter() }
    pub fn vals(&self) -> std::collections::hash_map::Values<'_, u8, A> { self.items.values() }
    pub fn res(&self) -> Result<&A, String> { self.list.first().ok_or(String::new()) }
}
pub struct Res<T>(T);
impl<T> Deref for Res<T> { type Target = T; fn deref(&self) -> &T { &self.0 } }
impl<T> DerefMut for Res<T> { fn deref_mut(&mut self) -> &mut T { &mut self.0 } }
pub struct Bag { pub foos: Vec<Foo> }
impl Bag {
    pub fn new() -> Self { Bag { foos: vec![Foo(1)] } }
    pub fn me(&self) -> &Self { self }
    pub fn first(&self) -> Option<&Foo> { self.foos.first() }
    pub fn maybe() -> Option<Self> { Some(Bag::new()) }
    pub fn tryit() -> Result<Self, String> { Ok(Bag::new()) }
    pub fn iter(&self) -> std::slice::Iter<'_, Foo> { self.foos.iter() }
    pub fn lookup(&self) -> HashMap<u8, Foo> { let mut m = HashMap::new(); m.insert(0, Foo(1)); m }
    pub fn pairs(&self) -> Vec<(u8, Foo)> { vec![(0, Foo(1))] }
    pub fn boxed(&self) -> Box<Foo> { Box::new(Foo(1)) }
    pub fn opt_vec(&self) -> Option<Vec<Foo>> { Some(vec![Foo(1)]) }
}
impl<'a> IntoIterator for &'a Bag { type Item = &'a Foo; type IntoIter = std::slice::Iter<'a, Foo>; fn into_iter(self) -> Self::IntoIter { self.foos.iter() } }
pub struct Counter { n: u8 }
impl Iterator for Counter { type Item = Foo; fn next(&mut self) -> Option<Foo> { if self.n == 0 { None } else { self.n -= 1; Some(Foo(self.n)) } } }
pub trait Source { type Out; fn produce(&self) -> Option<Self::Out>; }
pub struct Src;
impl Source for Src { type Out = Foo; fn produce(&self) -> Option<Foo> { Some(Foo(9)) } }
fn c_get(a: &Assets<Foo>) { a.get(Id(0)).unwrap().m(); }
fn c_get_iflet(a: &Assets<Foo>) { if let Some(x) = a.get(Id(0)) { x.m(); } }
fn c_get_letelse(a: &Assets<Foo>) { let Some(x) = a.get(Id(0)) else { return; }; x.m(); }
fn c_get_mut(a: &mut Assets<Foo>) { if let Some(x) = a.get_mut(Id(0)) { x.m(); } }
fn c_impl_iter(a: &Assets<Foo>) { for (_id, x) in a.iter() { x.m(); } }
fn c_impl_values(a: &Assets<Foo>) { for x in a.values() { x.m(); } }
fn c_impl_values_map(a: &Assets<Foo>) { let _: Vec<u8> = a.values().map(|x| x.m()).collect(); }
fn c_all(a: &Assets<Foo>) { for x in a.all() { x.m(); } }
fn c_all_iter(a: &Assets<Foo>) { for x in a.all().iter() { x.m(); } }
fn c_slice(a: &Assets<Foo>) { for x in a.slice() { x.m(); } }
fn c_first(a: &Assets<Foo>) { a.first().unwrap().m(); }
fn c_owned(a: &Assets<Foo>) { for x in a.owned() { x.m(); } }
fn c_res(a: &Assets<Foo>) { a.res().unwrap().m(); }
fn c_res_iflet(a: &Assets<Foo>) { if let Ok(x) = a.res() { x.m(); } }
fn c_deref_get(a: &Res<Assets<Foo>>) { a.get(Id(0)).unwrap().m(); }
fn c_deref_iflet(a: Res<Assets<Foo>>) { if let Some(x) = a.get(Id(0)) { x.m(); } }
fn c_deref_iter(a: Res<Assets<Foo>>) { for (_i, x) in a.iter() { x.m(); } }
fn c_let_chain(a: &Assets<Foo>) { let x = a.get(Id(0)); x.unwrap().m(); }
fn c_self_ret(b: &Bag) { b.me().first().unwrap().m(); }
fn c_maybe() { Bag::maybe().unwrap().first().unwrap().m(); }
fn c_try() -> Result<(), String> { Bag::tryit()?.first().unwrap().m(); Ok(()) }
fn c_bag_iter(b: &Bag) { for x in b.iter() { x.m(); } }
fn c_bag_into(b: &Bag) { for x in b { x.m(); } }
fn c_bag_lookup(b: &Bag) { for (_k, x) in b.lookup() { x.m(); } }
fn c_bag_lookup_get(b: &Bag) { b.lookup().get(&0).unwrap().m(); }
fn c_bag_pairs(b: &Bag) { for (_k, x) in b.pairs() { x.m(); } }
fn c_bag_pairs_iter(b: &Bag) { for (_k, x) in b.pairs().iter() { x.m(); } }
fn c_bag_boxed(b: &Bag) { b.boxed().m(); }
fn c_bag_opt_vec(b: &Bag) { for x in b.opt_vec().unwrap() { x.m(); } }
fn c_bag_opt_vec_iflet(b: &Bag) { if let Some(v) = b.opt_vec() { v[0].m(); } }
fn c_counter() { for x in (Counter { n: 1 }) { x.m(); } }
fn c_counter_next() { let mut c = Counter { n: 1 }; c.next().unwrap().m(); }
fn c_counter_map() { let _: Vec<u8> = (Counter { n: 1 }).map(|x| x.m()).collect(); }
fn c_assoc(s: &Src) { s.produce().unwrap().m(); }
fn c_assoc_iflet(s: &Src) { if let Some(x) = s.produce() { x.m(); } }
fn c_generic<S: Source<Out = Foo>>(s: &S) { s.produce().unwrap().m(); }
fn c_field_iter(b: &Bag) { b.foos.iter().for_each(|x| { x.m(); }); }
fn c_field_first(b: &Bag) { b.foos.first().unwrap().m(); }
fn c_field_index(b: &Bag) { b.foos[0].m(); }
fn c_let_vec(b: &Bag) { let v = b.foos.clone(); v[0].m(); }
fn c_let_iter_chain(b: &Bag) { let v: Vec<&Foo> = b.foos.iter().collect(); v[0].m(); }
pub struct Archs { v: Vec<Foo> }
pub struct ArchId(usize);
impl std::ops::Index<ArchId> for Archs { type Output = Foo; fn index(&self, i: ArchId) -> &Foo { &self.v[i.0] } }
fn c_index_ref(a: &Archs) { a[ArchId(0)].m(); }
fn c_index_own(a: Archs) { a[ArchId(0)].m(); }
fn c_entry_btree(m: &mut std::collections::BTreeMap<u8, Foo>) { m.entry(0).or_insert_with(|| Foo(1)).m(); }
fn c_entry_hash(m: &mut HashMap<u8, Foo>) { m.entry(0).or_insert(Foo(1)).m(); }
fn c_map_index_field(a: &Assets<Foo>) { a.items[&0].m(); }
fn c_drain_ret(a: &mut Assets<Foo>) { for (_k, x) in a.drained() { x.m(); } }
fn c_iter_ret(a: &Assets<Foo>) { for (_k, x) in a.pairs_iter() { x.m(); } }
fn c_vals_ret(a: &Assets<Foo>) { let _: Vec<u8> = a.vals().map(|x| x.m()).collect(); }
fn main() {
    let mut a: Assets<Foo> = Assets::default(); a.items.insert(0, Foo(1)); a.list.push(Foo(2));
    let b = Bag::new();
    c_get(&a); c_get_iflet(&a); c_get_letelse(&a); c_get_mut(&mut a); c_impl_iter(&a); c_impl_values(&a); c_impl_values_map(&a);
    c_all(&a); c_all_iter(&a); c_slice(&a); c_first(&a); c_owned(&a); c_res(&a); c_res_iflet(&a);
    let mut r = Res(Assets::default()); r.items.insert(0, Foo(1)); r.list.push(Foo(2));
    c_deref_get(&r); c_let_chain(&r); let r2 = Res(Assets { items: r.items.clone(), list: r.list.clone() }); c_deref_iflet(r);
    c_deref_iter(r2); c_self_ret(&b); c_maybe(); let _ = c_try(); c_bag_iter(&b); c_bag_into(&b); c_bag_lookup(&b); c_bag_lookup_get(&b);
    c_bag_pairs(&b); c_bag_pairs_iter(&b); c_bag_boxed(&b); c_bag_opt_vec(&b); c_bag_opt_vec_iflet(&b); c_counter(); c_counter_next(); c_counter_map();
    c_assoc(&Src); c_assoc_iflet(&Src); c_generic(&Src); c_field_iter(&b); c_field_first(&b); c_field_index(&b); c_let_vec(&b); c_let_iter_chain(&b);
    c_index_ref(&Archs { v: vec![Foo(1)] }); c_index_own(Archs { v: vec![Foo(1)] });
    c_entry_btree(&mut std::collections::BTreeMap::new()); c_entry_hash(&mut HashMap::new()); c_map_index_field(&a);
    c_iter_ret(&a); c_vals_ret(&a); c_drain_ret(&mut a);
    println!("end\n{}", std::backtrace::Backtrace::force_capture());
}
