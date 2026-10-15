pub struct Bar;
impl Bar {
    pub fn go(self) -> u8 { println!("inherent Bar::go(self)\n{}", std::backtrace::Backtrace::force_capture()); 1 }
}
pub trait Foo { fn go(self) -> u8; }
impl Foo for &Bar {
    fn go(self) -> u8 { println!("trait <&Bar as Foo>::go\n{}", std::backtrace::Backtrace::force_capture()); 2 }
}
pub fn probe(b: &Bar) { b.go(); }
fn main() { probe(&Bar); }
