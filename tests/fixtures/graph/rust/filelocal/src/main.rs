trait Other { fn go(&self); }
struct S;
impl S { fn go(&self) { println!("S::go inherent\n{}", std::backtrace::Backtrace::force_capture()); } }
struct U;
impl Other for U { fn go(&self) { println!("U Other::go\n{}", std::backtrace::Backtrace::force_capture()); } }
fn call(s: &S) { s.go(); }
fn callu(u: &U) { u.go(); }
fn main() { call(&S); callu(&U); }
