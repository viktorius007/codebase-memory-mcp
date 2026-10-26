pub struct Map<K, V> {
    pub keys: Vec<K>,
    pub values: Vec<V>,
}

impl<K, V> Map<K, V> {
    pub fn count(&self) -> usize {
        println!("Map::count\n{}", std::backtrace::Backtrace::force_capture());
        self.keys.len() + self.values.len()
    }
}

pub type Phases = Map<u32, u8>;
