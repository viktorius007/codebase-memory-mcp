pub mod a {
    use std::path::PathBuf;
    use std::collections::BTreeMap;
    pub struct S;
    impl S { pub fn m(&self) { println!("S::m inherent (&self)\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(self); }
    impl Tr for PathBuf { fn m(self) { println!("<PathBuf as Tr>::m\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub struct U;
    impl U { pub fn n(&self) { println!("U::n inherent (&self)\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tn { fn n(self); }
    impl Tn for BTreeMap<u8, u8> { fn n(self) { println!("<BTreeMap as Tn>::n\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub struct V;
    impl V { pub fn k(&self) { println!("V::k inherent (&self)\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tk { fn k(self); }
    impl Tk for std::net::Ipv4Addr { fn k(self) { println!("<Ipv4Addr as Tk>::k\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn own_s() { let v = S; v.m(); }
    pub fn own_u() { let v = U; v.n(); }
    pub fn own_v() { let v = V; v.k(); }
}
fn main() { a::own_s(); a::own_u(); a::own_v(); }
