pub struct S;
impl S { pub fn go(&self) { println!("S::go\n{}", std::backtrace::Backtrace::force_capture()); } }
pub fn by_ref(s: &S) { s.go(); }
fn main() { by_ref(&S); }
