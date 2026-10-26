pub struct Bag(Vec<u32>);
impl Default for Bag {
    fn default() -> Self { println!("Bag::default\n{}", std::backtrace::Backtrace::force_capture()); Bag(Vec::new()) }
}

pub struct Builder<'w, F> {
    world: &'w u8,
    filter: F,
}
pub fn build(world: &u8) -> Builder<'_, Bag> {
    Builder { world, filter: Default::default() }
}
fn main() {
    let w = 1;
    let b = build(&w);
    let _ = (*b.world, b.filter.0.len());
}
