pub struct Bag(Vec<u32>);
impl Default for Bag {
    fn default() -> Self { println!("Bag::default\n{}", std::backtrace::Backtrace::force_capture()); Bag(Vec::new()) }
}

pub struct Wrap(Bag);
impl Wrap {
    pub fn fresh() -> Self { Self(Default::default()) }
}
pub enum Slot { Full(Bag), Empty }
fn main() {
    let w = Wrap(Default::default());
    let s = Slot::Full(Default::default());
    let _ = (w.0 .0.len(), Wrap::fresh().0 .0.len(), matches!(s, Slot::Empty));
}
