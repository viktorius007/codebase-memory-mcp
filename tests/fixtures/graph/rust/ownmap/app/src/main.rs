macro_rules! gate {
    ($($t:tt)*) => {};
}
gate! {
    mod dep;
}
plat::disabled! {
    mod dep;
}
use dep::Thing;
use plat::collections::{HashMap, HashSet};
pub fn dropped(x: &Thing) { x.ping(); }
pub struct Holder { m: HashMap<u32, u32>, s: HashSet<u32> }
impl Holder {
    pub fn put(&mut self) { self.m.insert(1, 2); }
    pub fn look(&self) -> bool { self.m.get(&1).is_some() }
    pub fn mark(&mut self) -> bool { self.s.insert(7) }
    pub fn seen(&self) -> bool { self.s.contains(&7) }
}
pub fn by_param(m: &mut HashMap<u32, u32>) { m.retain(|_, _| true); }
pub fn by_set_param(s: &HashSet<u32>) -> bool { s.contains(&1) }
pub fn by_local() -> usize { let mut m: HashMap<u32, u32> = HashMap::new(); m.insert(3, 4); m.len() }
pub fn by_ctor() { let mut m = HashMap::new(); m.insert(5u32, 6u32); }
pub fn by_path(m: &plat::collections::HashMap<u32, u32>) -> bool { m.get(&5).is_some() }
fn main() {
    let mut h = Holder { m: HashMap::new(), s: HashSet::new() };
    h.put(); h.look(); h.mark(); h.seen();
    by_param(&mut h.m); by_set_param(&h.s); by_local(); by_ctor(); by_path(&h.m); dropped(&Thing);
    println!("end\n{}", std::backtrace::Backtrace::force_capture());
}
