pub struct S;
impl S {
    pub fn hash(&self) -> u64 { println!("S::hash inherent\n{}", std::backtrace::Backtrace::force_capture()); 7 }
    pub fn clone(&self) -> S { println!("S::clone inherent\n{}", std::backtrace::Backtrace::force_capture()); S }
    pub fn fmt(&self) { println!("S::fmt inherent\n{}", std::backtrace::Backtrace::force_capture()); }
}
pub fn call_hash(s: &S) -> u64 { s.hash() }
pub fn call_clone(s: S) -> S { s.clone() }
pub fn call_fmt(s: S) { s.fmt() }

// Unrelated types in the same module carrying derives.
#[derive(Hash, Clone, Debug)]
pub struct Other;
fn main() { call_hash(&S); call_clone(S); call_fmt(S); let _ = Other.clone(); }
