pub struct World { n: u32 }
impl World {
    pub fn tick(&mut self) { println!("World::tick\n{}", std::backtrace::Backtrace::force_capture()); self.n += 1; }
}
pub struct Value;
impl Value {
    pub fn as_partial(&self) -> u8 { println!("Value::as_partial\n{}", std::backtrace::Backtrace::force_capture()); 1 }
}
pub struct ReflectFns {
    pub set: fn(&mut World, &Value, u32),
    pub get: fn(&World) -> Option<&Value>,
}
pub fn fns() -> ReflectFns {
    ReflectFns {
        set: |world, value, _n| {
            world.tick();
            value.as_partial();
        },
        get: |_world| None,
    }
}
fn main() {
    let f = fns();
    let mut w = World { n: 0 };
    (f.set)(&mut w, &Value, 3);
    let _ = (f.get)(&w);
}
