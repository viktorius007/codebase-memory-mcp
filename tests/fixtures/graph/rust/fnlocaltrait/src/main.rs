struct S;
impl S { fn go(&self) { println!("S::go inherent &self\n{}", std::backtrace::Backtrace::force_capture()); } }
fn f() {
    trait Hid { fn go(self); }
    impl Hid for S { fn go(self) { println!("Hid::go (self)\n{}", std::backtrace::Backtrace::force_capture()); } }
    let s = S; s.go();
}
fn g() { let s = S; s.go(); }
fn main() { f(); g(); }
