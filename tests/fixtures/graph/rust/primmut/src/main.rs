pub trait Bump { fn bump(&mut self); }
impl Bump for u8 { fn bump(&mut self) { println!("<u8 as Bump>::bump\n{}", std::backtrace::Backtrace::force_capture()); *self += 1; } }
pub fn owned() { let mut x = 1u8; x.bump(); }
pub fn by_mut(x: &mut u8) { x.bump(); }
fn main() { owned(); by_mut(&mut 3); }
