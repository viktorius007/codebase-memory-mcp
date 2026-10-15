pub mod a {
    pub struct S;
    impl S { pub fn go(self) { println!("S::go inherent (self)\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Hidden { fn go(&self); }
    impl Hidden for S { fn go(&self) { println!("<S as Hidden>::go\n{}", std::backtrace::Backtrace::force_capture()); } }

    #[derive(Hash)]
    pub struct H;
    impl H { pub fn hash(&self) { println!("H::hash inherent (&self)\n{}", std::backtrace::Backtrace::force_capture()); } }

    pub struct T;
    pub trait Vis { fn run(&self); }
    impl Vis for T { fn run(&self) { println!("<T as Vis>::run\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Hid { fn run(&self); }
    impl Hid for T { fn run(&self) { println!("<T as Hid>::run\n{}", std::backtrace::Backtrace::force_capture()); } }
}
pub mod b {
    pub fn call_go(s: crate::a::S) { s.go() }
    pub fn call_hash(h: &crate::a::H) { h.hash() }
    use crate::a::Vis;
    pub fn call_run(t: &crate::a::T) { t.run() }
}
fn main() { b::call_go(a::S); b::call_hash(&a::H); b::call_run(&a::T); }
