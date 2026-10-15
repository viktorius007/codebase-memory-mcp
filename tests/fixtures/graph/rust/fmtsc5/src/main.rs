use std::fmt;
pub struct NoDbg;
impl fmt::Display for NoDbg {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result { write!(f, "NoDbg Display") }
}
pub fn owned1(f: &mut fmt::Formatter<'_>) -> fmt::Result { use fmt::Display; let v = NoDbg; v.fmt(f) }
#[derive(Debug)]
pub struct Other;
fn main() { println!("{:?}\n{}", Other, std::backtrace::Backtrace::force_capture()); }
