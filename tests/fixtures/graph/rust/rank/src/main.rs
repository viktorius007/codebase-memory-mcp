#[derive(Clone, Copy)] pub struct A;
impl A { pub fn pick(self) -> u8 { println!("A::pick inherent (self)\n{}", std::backtrace::Backtrace::force_capture()); 1 } }
pub trait PickA { fn pick(&self) -> u8 { println!("PickA::pick default (&self)\n{}", std::backtrace::Backtrace::force_capture()); 2 } }
impl PickA for A {}
pub fn probe_default(v: &A) { v.pick(); }

#[derive(Clone, Copy)] pub struct B;
impl B { pub fn pick(self) -> u8 { println!("B::pick inherent (self)\n{}", std::backtrace::Backtrace::force_capture()); 1 } }
pub trait PickB { fn pick(&self) -> u8; }
impl PickB for B { fn pick(&self) -> u8 { println!("<B as PickB>::pick override (&self)\n{}", std::backtrace::Backtrace::force_capture()); 2 } }
pub fn probe_override(v: &B) { v.pick(); }
fn main() { probe_default(&A); probe_override(&B); }
