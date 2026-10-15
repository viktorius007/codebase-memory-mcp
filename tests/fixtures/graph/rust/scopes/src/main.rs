// Each case: inherent go(&self) on S; trait go(self) for S. rustc picks the trait (by-value rank) iff in scope.
pub mod traits { pub trait Tr { fn go(self); } pub trait Tr2 { fn go(self); } pub trait Tr3 { fn go(self); } }
pub mod reexp { pub use crate::traits::Tr2; }
pub mod types {
    pub struct S1; pub struct S2; pub struct S3; pub struct S4; pub struct S5; pub struct S6; pub struct S7;
    impl S1 { pub fn go(&self) { println!("S1 inherent\n{}", std::backtrace::Backtrace::force_capture()); } }
    impl S2 { pub fn go(&self) { println!("S2 inherent\n{}", std::backtrace::Backtrace::force_capture()); } }
    impl S3 { pub fn go(&self) { println!("S3 inherent\n{}", std::backtrace::Backtrace::force_capture()); } }
    impl S4 { pub fn go(&self) { println!("S4 inherent\n{}", std::backtrace::Backtrace::force_capture()); } }
    impl S5 { pub fn go(&self) { println!("S5 inherent\n{}", std::backtrace::Backtrace::force_capture()); } }
    impl S6 { pub fn go(&self) { println!("S6 inherent\n{}", std::backtrace::Backtrace::force_capture()); } }
    impl S7 { pub fn go(&self) { println!("S7 inherent\n{}", std::backtrace::Backtrace::force_capture()); } }
    impl crate::traits::Tr for S1 { fn go(self) { println!("S1 Tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    impl crate::traits::Tr for S2 { fn go(self) { println!("S2 Tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    impl crate::traits::Tr2 for S3 { fn go(self) { println!("S3 Tr2\n{}", std::backtrace::Backtrace::force_capture()); } }
    impl crate::traits::Tr for S4 { fn go(self) { println!("S4 Tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    impl crate::traits::Tr for S5 { fn go(self) { println!("S5 Tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    impl crate::traits::Tr3 for S6 { fn go(self) { println!("S6 Tr3\n{}", std::backtrace::Backtrace::force_capture()); } }
    impl crate::traits::Tr for S7 { fn go(self) { println!("S7 Tr\n{}", std::backtrace::Backtrace::force_capture()); } }
}
pub mod glob { use crate::traits::*; use crate::types::S1; pub fn call() { S1.go(); } }
pub mod anon { use crate::traits::Tr as _; use crate::types::S2; pub fn call() { S2.go(); } }
pub mod viareexp { use crate::reexp::Tr2; use crate::types::S3; pub fn call() { S3.go(); } }
pub mod infn { use crate::types::S4; pub fn call() { use crate::traits::Tr; S4.go(); } }
pub mod modalias { use crate::traits as tt; use tt::Tr; use crate::types::S5; pub fn call() { S5.go(); } }
pub mod outscope { use crate::types::S6; pub fn call() { S6.go(); } }
pub mod parent {
    use crate::traits::Tr;
    pub mod child { use super::*; use crate::types::S7; pub fn call() { S7.go(); } }
}
fn main() { glob::call(); anon::call(); viareexp::call(); infn::call(); modalias::call(); outscope::call(); parent::child::call(); }
