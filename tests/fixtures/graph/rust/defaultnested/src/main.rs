use std::collections::HashMap;
pub struct Bag(Vec<u32>);
impl Default for Bag {
    fn default() -> Self { println!("Bag::default\n{}", std::backtrace::Backtrace::force_capture()); Bag(Vec::new()) }
}

pub fn fill(target: &mut Bag) -> usize { target.0.len() }
pub struct Other(u8);
impl Default for Other {
    fn default() -> Self { println!("Other::default\n{}", std::backtrace::Backtrace::force_capture()); Other(0) }
}
pub struct Maker;
impl Maker {
    pub fn unwrap_or_else(&self, make: fn() -> Bag) -> Other { Other(make().0.len() as u8) }
}

fn main() {
    let pair: (u8, Bag) = (1, Default::default());
    let n = fill(&mut Default::default());
    let mut nodes: HashMap<u32, (u32, Bag)> = HashMap::new();
    nodes.entry(3).or_insert_with(|| (4, Default::default()));
    let missing: Option<Bag> = None;
    let bag = missing.unwrap_or_else(|| Default::default());
    let other = Maker.unwrap_or_else(|| Default::default());
    let _ = (pair.0, n, nodes.len(), bag.0.len(), other.0);
}
