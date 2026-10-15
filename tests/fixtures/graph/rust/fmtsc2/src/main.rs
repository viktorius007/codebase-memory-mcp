use std::fmt;
pub struct NoDbg;
impl fmt::Display for NoDbg {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result { write!(f, "NoDbg Display") }
}
#[derive(Debug)]
pub struct WithDbg;
impl fmt::Display for WithDbg {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result { write!(f, "WithDbg Display") }
}
pub enum Outer1 { W(NoDbg) }
impl fmt::Display for Outer1 {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result { match self { Self::W(r) => r.fmt(f) } }
}
pub enum Outer2 { W(WithDbg) }
impl fmt::Display for Outer2 {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result { match self { Self::W(r) => r.fmt(f) } }
}
pub fn owned1(f: &mut fmt::Formatter<'_>) -> fmt::Result { use fmt::Display; let v = NoDbg; v.fmt(f) }
fn main() { println!("{}\n{}", Outer1::W(NoDbg), std::backtrace::Backtrace::force_capture()); println!("{}\n{}", Outer2::W(WithDbg), std::backtrace::Backtrace::force_capture()); }
