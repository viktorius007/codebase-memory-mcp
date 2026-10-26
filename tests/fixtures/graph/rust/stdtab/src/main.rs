use std::collections::{BTreeMap, HashMap, HashSet, VecDeque};
use std::rc::Rc;
use std::sync::{Arc, Mutex, RwLock};
#[derive(Clone, Default, PartialEq, Eq, Hash, PartialOrd, Ord, Debug)]
pub struct Foo(u8);
impl Foo {
    pub fn m(&self) -> u8 { println!("Foo::m\n{}", std::backtrace::Backtrace::force_capture()); self.0 }
    pub fn mk() -> Option<Foo> { Some(Foo(1)) }
    pub fn rk() -> Result<Foo, String> { Ok(Foo(2)) }
    pub fn me(&self) -> Option<&Foo> { Some(self) }
    pub fn kids(&self) -> Vec<Foo> { vec![Foo(3)] }
    pub fn kid_slice(&self) -> &[Foo] { std::slice::from_ref(self) }
}
pub struct Holder { pub v: Vec<Foo>, pub o: Option<Foo>, pub map: HashMap<u8, Foo>, pub b: Box<Foo>, pub rc: Rc<Foo>, pub am: Arc<Mutex<Foo>> }
fn s_for_ref_vec(v: &Vec<Foo>) { for x in v { x.m(); } }
fn s_for_iter(v: &Vec<Foo>) { for x in v.iter() { x.m(); } }
fn s_for_iter_mut(v: &mut Vec<Foo>) { for x in v.iter_mut() { x.m(); } }
fn s_for_into_iter(v: Vec<Foo>) { for x in v.into_iter() { x.m(); } }
fn s_for_owned(v: Vec<Foo>) { for x in v { x.m(); } }
fn s_for_slice(v: &[Foo]) { for x in v { x.m(); } }
fn s_for_array(v: [Foo; 1]) { for x in v { x.m(); } }
fn s_for_enum(v: &[Foo]) { for (_i, x) in v.iter().enumerate() { x.m(); } }
fn s_for_map(m: &HashMap<u8, Foo>) { for (_k, x) in m { x.m(); } }
fn s_for_map_iter(m: &HashMap<u8, Foo>) { for (_k, x) in m.iter() { x.m(); } }
fn s_for_values(m: &HashMap<u8, Foo>) { for x in m.values() { x.m(); } }
fn s_for_keys(m: &BTreeMap<Foo, u8>) { for x in m.keys() { x.m(); } }
fn s_for_set(m: &HashSet<Foo>) { for x in m { x.m(); } }
fn s_for_deque(m: &VecDeque<Foo>) { for x in m.iter() { x.m(); } }
fn s_for_zip(a: &[Foo], b: &[u8]) { for (x, _y) in a.iter().zip(b.iter()) { x.m(); } }
fn s_for_chain(a: &[Foo], b: &[Foo]) { for x in a.iter().chain(b.iter()) { x.m(); } }
fn s_for_rev(a: &[Foo]) { for x in a.iter().rev() { x.m(); } }
fn s_for_skip(a: &[Foo]) { for x in a.iter().skip(0) { x.m(); } }
fn s_for_filter(a: &[Foo]) { for x in a.iter().filter(|_| true) { x.m(); } }
fn s_for_kids(f: &Foo) { for x in f.kids() { x.m(); } }
fn s_for_kid_slice(f: &Foo) { for x in f.kid_slice() { x.m(); } }
fn s_closure_map(a: &[Foo]) { let _: Vec<u8> = a.iter().map(|x| x.m()).collect(); }
fn s_closure_filter(a: &[Foo]) { let _ = a.iter().filter(|x| x.m() > 0).count(); }
fn s_closure_for_each(a: &[Foo]) { a.iter().for_each(|x| { x.m(); }); }
fn s_closure_any(a: &[Foo]) { let _ = a.iter().any(|x| x.m() > 0); }
fn s_closure_find(a: &[Foo]) { let _ = a.iter().find(|x| x.m() > 0); }
fn s_closure_filter_map(a: &[Foo]) { let _: Vec<u8> = a.iter().filter_map(|x| Some(x.m())).collect(); }
fn s_closure_vals(m: &HashMap<u8, Foo>) { let _: Vec<u8> = m.values().map(|x| x.m()).collect(); }
fn s_closure_map_iter(m: &HashMap<u8, Foo>) { let _: Vec<u8> = m.iter().map(|(_, x)| x.m()).collect(); }
fn s_closure_opt_map(o: Option<Foo>) { let _ = o.map(|x| x.m()); }
fn s_closure_opt_ref_map(o: &Option<Foo>) { let _ = o.as_ref().map(|x| x.m()); }
fn s_closure_res_map(o: Result<Foo, String>) { let _ = o.map(|x| x.m()); }
fn s_closure_is_some_and(o: Option<Foo>) { let _ = o.is_some_and(|x| x.m() > 0); }
fn s_closure_enum_map(a: &[Foo]) { let _: Vec<u8> = a.iter().enumerate().map(|(_, x)| x.m()).collect(); }
fn s_iflet_opt(o: Option<Foo>) { if let Some(x) = o { x.m(); } }
fn s_iflet_ref(o: &Option<Foo>) { if let Some(x) = o { x.m(); } }
fn s_iflet_get(v: &Vec<Foo>) { if let Some(x) = v.get(0) { x.m(); } }
fn s_iflet_first(v: &[Foo]) { if let Some(x) = v.first() { x.m(); } }
fn s_iflet_last(v: &Vec<Foo>) { if let Some(x) = v.last() { x.m(); } }
fn s_iflet_map_get(m: &HashMap<u8, Foo>) { if let Some(x) = m.get(&0) { x.m(); } }
fn s_iflet_btree_get(m: &BTreeMap<u8, Foo>) { if let Some(x) = m.get(&0) { x.m(); } }
fn s_iflet_get_mut(m: &mut HashMap<u8, Foo>) { if let Some(x) = m.get_mut(&0) { x.m(); } }
fn s_iflet_ok(o: Result<Foo, String>) { if let Ok(x) = o { x.m(); } }
fn s_iflet_call() { if let Some(x) = Foo::mk() { x.m(); } }
fn s_iflet_method(f: &Foo) { if let Some(x) = f.me() { x.m(); } }
fn s_iflet_next(v: &[Foo]) { if let Some(x) = v.iter().next() { x.m(); } }
fn s_letelse(o: Option<Foo>) { let Some(x) = o else { return; }; x.m(); }
fn s_letelse_get(m: &HashMap<u8, Foo>) { let Some(x) = m.get(&0) else { return; }; x.m(); }
fn s_while_let(v: &mut Vec<Foo>) { while let Some(x) = v.pop() { x.m(); } }
fn s_match_opt(o: Option<Foo>) { match o { Some(x) => { x.m(); } None => {} } }
fn s_unwrap(o: Option<Foo>) { o.unwrap().m(); }
fn s_expect(o: Result<Foo, String>) { o.expect("x").m(); }
fn s_unwrap_call() { Foo::mk().unwrap().m(); }
fn s_let_unwrap() { let x = Foo::rk().unwrap(); x.m(); }
fn s_try() -> Option<u8> { let x = Foo::mk()?; Some(x.m()) }
fn s_try_res() -> Result<u8, String> { Ok(Foo::rk()?.m()) }
fn s_get_unwrap(v: &Vec<Foo>) { v.get(0).unwrap().m(); }
fn s_index(v: &Vec<Foo>) { v[0].m(); }
fn s_map_index(m: &HashMap<u8, Foo>) { m[&0].m(); }
fn s_first_unwrap(v: &[Foo]) { v.first().unwrap().m(); }
fn s_iter_next(v: &[Foo]) { v.iter().next().unwrap().m(); }
fn s_iter_last(v: &[Foo]) { v.iter().last().unwrap().m(); }
fn s_find_unwrap(v: &[Foo]) { v.iter().find(|_| true).unwrap().m(); }
fn s_max(v: &[Foo]) { v.iter().max().unwrap().m(); }
fn s_box(b: Box<Foo>) { b.m(); }
fn s_rc(b: Rc<Foo>) { b.m(); }
fn s_arc(b: Arc<Foo>) { b.m(); }
fn s_mutex(b: &Mutex<Foo>) { b.lock().unwrap().m(); }
fn s_rwlock(b: &RwLock<Foo>) { b.read().unwrap().m(); }
fn s_arc_mutex(b: Arc<Mutex<Foo>>) { b.lock().unwrap().m(); }
fn s_as_ref(o: &Option<Foo>) { o.as_ref().unwrap().m(); }
fn s_as_mut(o: &mut Option<Foo>) { o.as_mut().unwrap().m(); }
fn s_clone(f: &Foo) { f.clone().m(); }
fn s_clone_vec(v: &Vec<Foo>) { v.clone()[0].m(); }
fn s_field_opt(h: &Holder) { h.o.as_ref().unwrap().m(); }
fn s_field_vec(h: &Holder) { for x in &h.v { x.m(); } }
fn s_field_map(h: &Holder) { h.map.get(&0).unwrap().m(); }
fn s_field_box(h: &Holder) { h.b.m(); }
fn s_field_rc(h: &Holder) { h.rc.m(); }
fn s_field_am(h: &Holder) { h.am.lock().unwrap().m(); }
fn s_entry(m: &mut HashMap<u8, Foo>) { m.entry(0).or_default().m(); }
fn s_let_get(m: &HashMap<u8, Foo>) { let x = m.get(&0).unwrap(); x.m(); }
fn s_vec_new() { let mut v = Vec::new(); v.push(Foo(1)); v[0].m(); }
fn s_vec_macro() { let v = vec![Foo(1)]; v[0].m(); }
fn s_collect(v: &[Foo]) { let w: Vec<Foo> = v.to_vec(); w[0].m(); }
fn s_chunks(v: &[Foo]) { for c in v.chunks(1) { c[0].m(); } }
fn s_windows(v: &[Foo]) { for c in v.windows(1) { c[0].m(); } }
fn s_split_first(v: &[Foo]) { if let Some((x, _)) = v.split_first() { x.m(); } }
fn s_cloned(v: &[Foo]) { v.iter().cloned().next().unwrap().m(); }
fn s_deref_star(b: &Box<Foo>) { (**b).m(); }
fn s_ref_pat(v: &[Foo]) { for &ref x in v { x.m(); } }
fn s_opt_ref(o: Option<&Foo>) { if let Some(x) = o { x.m(); } }
fn s_tuple(v: &[(u8, Foo)]) { for (_a, x) in v { x.m(); } }
fn s_tuple_closure(v: &[(u8, Foo)]) { v.iter().for_each(|(_a, x)| { x.m(); }); }
fn s_sort_by(v: &mut Vec<Foo>) { v.sort_by(|a, b| a.m().cmp(&b.m())); }
fn s_retain(v: &mut Vec<Foo>) { v.retain(|x| x.m() > 0); }
fn s_drain(v: &mut Vec<Foo>) { for x in v.drain(..) { x.m(); } }
fn s_flat_map(v: &[Foo]) { let _: Vec<u8> = v.iter().flat_map(|x| x.kids()).map(|y| y.m()).collect(); }
fn s_iter_peek(v: &[Foo]) { let mut it = v.iter().peekable(); if let Some(x) = it.peek() { x.m(); } }
fn s_let_iter(v: &[Foo]) { let mut it = v.iter(); while let Some(x) = it.next() { x.m(); } }
fn s_mem_take_for(v: &mut Vec<Foo>) { for x in std::mem::take(v) { x.m(); } }
fn s_mem_take_opt(o: &mut Option<Foo>) { if let Some(x) = std::mem::take(o) { x.m(); } }
fn s_mem_replace(f: &mut Foo) { std::mem::replace(f, Foo(2)).m(); }
fn s_get_mut_vec(v: &mut Vec<Foo>) { v.get_mut(0).unwrap().m(); }
fn s_get_mut_slice(v: &mut [Foo], i: usize) { if let Some(x) = v.get_mut(i) { x.m(); } }
fn main() {
    let f = Foo(1); let v = vec![Foo(1)]; let mut vm = vec![Foo(1), Foo(2)]; let a = [Foo(1)];
    let mut hm: HashMap<u8, Foo> = HashMap::new(); hm.insert(0, Foo(1));
    let mut bt: BTreeMap<Foo, u8> = BTreeMap::new(); bt.insert(Foo(1), 0);
    let mut bt2: BTreeMap<u8, Foo> = BTreeMap::new(); bt2.insert(0, Foo(1));
    let mut hs = HashSet::new(); hs.insert(Foo(1)); let dq: VecDeque<Foo> = v.iter().cloned().collect();
    let h = Holder { v: v.clone(), o: Some(Foo(1)), map: hm.clone(), b: Box::new(Foo(1)), rc: Rc::new(Foo(1)), am: Arc::new(Mutex::new(Foo(1))) };
    s_for_ref_vec(&v); s_for_iter(&v); s_for_iter_mut(&mut vm); s_for_into_iter(v.clone()); s_for_owned(v.clone()); s_for_slice(&v); s_for_array(a.clone());
    s_for_enum(&v); s_for_map(&hm); s_for_map_iter(&hm); s_for_values(&hm); s_for_keys(&bt); s_for_set(&hs); s_for_deque(&dq);
    s_for_zip(&v, &[1]); s_for_chain(&v, &v); s_for_rev(&v); s_for_skip(&v); s_for_filter(&v); s_for_kids(&f); s_for_kid_slice(&f);
    s_closure_map(&v); s_closure_filter(&v); s_closure_for_each(&v); s_closure_any(&v); s_closure_find(&v); s_closure_filter_map(&v);
    s_closure_vals(&hm); s_closure_map_iter(&hm); s_closure_opt_map(Some(Foo(1))); s_closure_opt_ref_map(&Some(Foo(1))); s_closure_res_map(Ok(Foo(1)));
    s_closure_is_some_and(Some(Foo(1))); s_closure_enum_map(&v);
    s_iflet_opt(Some(Foo(1))); s_iflet_ref(&Some(Foo(1))); s_iflet_get(&v); s_iflet_first(&v); s_iflet_last(&v); s_iflet_map_get(&hm); s_iflet_btree_get(&bt2);
    s_iflet_get_mut(&mut hm); s_iflet_ok(Ok(Foo(1))); s_iflet_call(); s_iflet_method(&f); s_iflet_next(&v); s_letelse(Some(Foo(1))); s_letelse_get(&hm);
    s_while_let(&mut vm.clone()); s_match_opt(Some(Foo(1))); s_unwrap(Some(Foo(1))); s_expect(Ok(Foo(1))); s_unwrap_call(); s_let_unwrap(); s_try(); let _ = s_try_res();
    s_get_unwrap(&v); s_index(&v); s_map_index(&hm); s_first_unwrap(&v); s_iter_next(&v); s_iter_last(&v); s_find_unwrap(&v); s_max(&v);
    s_box(Box::new(Foo(1))); s_rc(Rc::new(Foo(1))); s_arc(Arc::new(Foo(1))); s_mutex(&Mutex::new(Foo(1))); s_rwlock(&RwLock::new(Foo(1))); s_arc_mutex(Arc::new(Mutex::new(Foo(1))));
    s_as_ref(&Some(Foo(1))); s_as_mut(&mut Some(Foo(1))); s_clone(&f); s_clone_vec(&v);
    s_field_opt(&h); s_field_vec(&h); s_field_map(&h); s_field_box(&h); s_field_rc(&h); s_field_am(&h); s_entry(&mut hm); s_let_get(&hm);
    s_vec_new(); s_vec_macro(); s_collect(&v); s_chunks(&v); s_windows(&v); s_split_first(&v); s_cloned(&v); s_deref_star(&Box::new(Foo(1))); s_ref_pat(&v);
    s_opt_ref(Some(&f)); s_tuple(&[(0, Foo(1))]); s_tuple_closure(&[(0, Foo(1))]); s_sort_by(&mut vm); s_retain(&mut vm.clone()); s_drain(&mut vm.clone());
    s_flat_map(&v); s_iter_peek(&v); s_let_iter(&v);
    s_mem_take_for(&mut vec![Foo(1)]); s_mem_take_opt(&mut Some(Foo(1))); s_mem_replace(&mut Foo(1));
    s_get_mut_vec(&mut vec![Foo(1)]); s_get_mut_slice(&mut [Foo(1)], 0);
    println!("end\n{}", std::backtrace::Backtrace::force_capture());
}
