pub mod a { pub fn f() { println!("a::f\n{}", std::backtrace::Backtrace::force_capture()); }
pub fn g() { println!("a::g\n{}", std::backtrace::Backtrace::force_capture()); } }
pub mod b { pub fn f() { println!("b::f\n{}", std::backtrace::Backtrace::force_capture()); } }
pub fn outer1() {
 fn helper() { println!("outer1::helper\n{}", std::backtrace::Backtrace::force_capture()); }
 helper(); }
pub fn outer2() {
 fn helper() { println!("outer2::helper\n{}", std::backtrace::Backtrace::force_capture()); }
 helper(); }
pub fn outer3() {
 {
 fn helper() { println!("outer3 inner-block helper\n{}", std::backtrace::Backtrace::force_capture()); }
 helper(); }
 helper();
 fn helper() { println!("outer3 fn-level helper\n{}", std::backtrace::Backtrace::force_capture()); }
 }
pub fn alias_call() { use crate::a::f as g; g(); }
pub fn shadow_glob() { use crate::a::*; use crate::b::f; f(); g(); }
pub enum E { One(Inner), Two }
pub struct Inner;
impl Inner { pub fn tag(&self) { println!("Inner::tag\n{}", std::backtrace::Backtrace::force_capture()); } }
impl E { pub fn tag(&self) { println!("E::tag\n{}", std::backtrace::Backtrace::force_capture()); } }
pub fn capture(e: &E) { if let x @ E::One(_) = e { x.tag(); } }
pub fn capture_inner(e: &E) { if let E::One(i @ Inner) = e { i.tag(); } }
pub trait Make { fn make() -> Self; }
pub struct P; pub struct Q;
impl Make for P { fn make() -> Self { println!("P::make\n{}", std::backtrace::Backtrace::force_capture()); P } }
impl Make for Q { fn make() -> Self { println!("Q::make\n{}", std::backtrace::Backtrace::force_capture()); Q } }
impl P { pub fn hello(&self) { println!("P::hello\n{}", std::backtrace::Backtrace::force_capture()); } }
impl Q { pub fn hello(&self) { println!("Q::hello\n{}", std::backtrace::Backtrace::force_capture()); } }
pub fn build<T: Make>() -> T { T::make() }
pub fn generic_ret() { let p: P = build(); p.hello(); let q = build::<Q>(); q.hello(); }
pub fn default_ret() { let p = <P as Make>::make(); p.hello(); let q: Q = Make::make(); q.hello(); }
fn main() { outer1(); outer2(); outer3(); alias_call(); shadow_glob(); capture(&E::One(Inner)); capture_inner(&E::One(Inner)); generic_ret(); default_ret(); }
