use std::fmt;
pub struct NoDbg;
impl fmt::Display for NoDbg {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result { write!(f, "NoDbg Display") }
}
pub fn owned1(f: &mut fmt::Formatter<'_>) -> fmt::Result { use fmt::Display; let v = NoDbg; v.fmt(f) }
pub struct Show(pub u8);
impl fmt::Display for Show {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result { owned1(f) }
}
// An unrelated type in the same module with a Debug impl.
pub struct Other;
impl fmt::Debug for Other {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result { write!(f, "Other") }
}
fn main() { println!("{}\n{}", Show(0), std::backtrace::Backtrace::force_capture()); println!("{:?}\n{}", Other, std::backtrace::Backtrace::force_capture()); }
