use std::collections::HashMap;
use std::ops::{Deref, DerefMut};

pub struct World;
impl World {
    pub fn tick(&self) -> u8 { println!("World::tick\n{}", std::backtrace::Backtrace::force_capture()); 1 }
}
pub struct QueryState<D> { d: D }
impl<D> QueryState<D> {
    pub fn iter_mut(&mut self) -> u8 { println!("QueryState::iter_mut\n{}", std::backtrace::Backtrace::force_capture()); 2 }
}

pub struct Methods(HashMap<String, u32>);
impl Methods {
    pub fn add(&mut self) -> Option<u32> { self.0.insert(String::new(), 1) }
}

pub struct Bench<'w>(World, QueryState<(&'w u32,)>);
impl<'w> Bench<'w> {
    pub fn run(&mut self) -> u8 { self.1.iter_mut() + self.0.tick() }
}

pub struct ResMut<T> { value: T }
impl<T> Deref for ResMut<T> { type Target = T; fn deref(&self) -> &T { &self.value } }
impl<T> DerefMut for ResMut<T> { fn deref_mut(&mut self) -> &mut T { &mut self.value } }

pub struct Holder(World);
pub fn through_wrapper(res: ResMut<Holder>) -> u8 { res.0.tick() }

fn main() {
    let mut m = Methods(HashMap::new());
    m.add();
    let x = 3u32;
    let mut b = Bench(World, QueryState { d: (&x,) });
    b.run();
    through_wrapper(ResMut { value: Holder(World) });
    let _ = (b.1.d, m.0.len());
}
