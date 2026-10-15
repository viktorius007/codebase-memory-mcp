mod a { pub fn f() { println!("a::f\n{}", std::backtrace::Backtrace::force_capture()); }
 pub fn drop<T>(_t: T) { println!("a::drop\n{}", std::backtrace::Backtrace::force_capture()); } }
mod b { pub fn f() { println!("b::f\n{}", std::backtrace::Backtrace::force_capture()); } }
mod c {
    use crate::a::*;
    pub fn f() { println!("c::f (local shadows glob)\n{}", std::backtrace::Backtrace::force_capture()); }
    pub fn call() { f(); }
    pub fn call_drop() { drop(1u8); }
}
mod d { pub fn call_drop() { drop(1u8); println!("d: std drop\n{}", std::backtrace::Backtrace::force_capture()); } }
mod x { pub use crate::y::g as h; }
mod y { pub fn g() { println!("y::g\n{}", std::backtrace::Backtrace::force_capture()); } }
mod e { pub fn call() { crate::x::h(); } }
#[path = "deep/other.rs"] mod other;
mod f2 {
    pub fn f() { println!("f2::f\n{}", std::backtrace::Backtrace::force_capture()); }
    pub mod inner {
        pub fn f() { println!("f2::inner::f\n{}", std::backtrace::Backtrace::force_capture()); }
        pub fn call() { super::f(); self::f(); f(); }
    }
}
fn main() { c::call(); c::call_drop(); d::call_drop(); e::call(); other::call(); f2::inner::call(); }
