use core::hash::Hash;
use std::collections::hash_set as hb;
pub struct HashSet<T>(hb::HashSet<T>);
impl<T> HashSet<T> {
    pub fn new() -> Self { println!("HashSet::new\n{}", std::backtrace::Backtrace::force_capture()); Self(hb::HashSet::new()) }
}
impl<T: Eq + Hash> HashSet<T> {
    pub fn insert(&mut self, v: T) -> bool { println!("HashSet::insert\n{}", std::backtrace::Backtrace::force_capture()); self.0.insert(v) }
    pub fn contains(&self, v: &T) -> bool { println!("HashSet::contains\n{}", std::backtrace::Backtrace::force_capture()); self.0.contains(v) }
}
