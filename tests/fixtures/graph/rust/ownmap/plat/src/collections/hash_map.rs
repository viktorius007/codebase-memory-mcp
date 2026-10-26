use core::hash::{BuildHasher, Hash};
use core::ops::{Deref, DerefMut};
use std::collections::hash_map as hb;
#[derive(Default, Clone, Copy)]
pub struct FixedHasher;
impl BuildHasher for FixedHasher { type Hasher = hb::DefaultHasher; fn build_hasher(&self) -> hb::DefaultHasher { hb::DefaultHasher::new() } }
pub struct HashMap<K, V, S = FixedHasher>(hb::HashMap<K, V, S>);
impl<K, V, S> Deref for HashMap<K, V, S> { type Target = hb::HashMap<K, V, S>; fn deref(&self) -> &Self::Target { &self.0 } }
impl<K, V, S> DerefMut for HashMap<K, V, S> { fn deref_mut(&mut self) -> &mut Self::Target { &mut self.0 } }
impl<K, V> HashMap<K, V, FixedHasher> {
    pub fn new() -> Self { println!("HashMap::new\n{}", std::backtrace::Backtrace::force_capture()); Self(hb::HashMap::with_hasher(FixedHasher)) }
}
impl<K, V, S> HashMap<K, V, S>
where
    K: Eq + Hash,
    S: BuildHasher,
{
    pub fn insert(&mut self, k: K, v: V) -> Option<V> { println!("HashMap::insert\n{}", std::backtrace::Backtrace::force_capture()); self.0.insert(k, v) }
    pub fn get(&self, k: &K) -> Option<&V> { println!("HashMap::get\n{}", std::backtrace::Backtrace::force_capture()); self.0.get(k) }
    pub fn retain<F: FnMut(&K, &mut V) -> bool>(&mut self, f: F) { println!("HashMap::retain\n{}", std::backtrace::Backtrace::force_capture()); self.0.retain(f) }
}
