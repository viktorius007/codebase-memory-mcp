use std::cmp::Ordering;
pub struct LineColumn { pub line: u32 }
impl PartialOrd for LineColumn {
    fn partial_cmp(&self, other: &Self) -> Option<Ordering> { Some(self.cmp(other)) }
}
impl Ord for LineColumn {
    fn cmp(&self, other: &Self) -> Ordering { println!("<LineColumn as Ord>::cmp\n{}", std::backtrace::Backtrace::force_capture()); self.line.cmp(&other.line) }
}
impl PartialEq for LineColumn { fn eq(&self, other: &Self) -> bool { self.cmp(other) == Ordering::Equal } }
impl Eq for LineColumn {}
// Another type in the same module with derived ordering.
#[derive(PartialEq, Eq, PartialOrd, Ord)]
pub struct Other(u8);
fn main() { let a = LineColumn { line: 1 }; let b = LineColumn { line: 2 }; let _ = a < b; let _ = a == b; }
