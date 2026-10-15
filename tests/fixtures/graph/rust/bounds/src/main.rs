pub trait Super { fn go(&self) { println!("Super::go default\n{}", std::backtrace::Backtrace::force_capture()); } }
pub trait Sub: Super { fn sub(&self) { println!("Sub::sub default\n{}", std::backtrace::Backtrace::force_capture()); } }
pub trait Other { fn go(&self) { println!("Other::go default\n{}", std::backtrace::Backtrace::force_capture()); } }
pub struct S;
impl Super for S {}
impl Sub for S {}
impl Other for S {}
impl S { pub fn hello(&self) { println!("S::hello\n{}", std::backtrace::Backtrace::force_capture()); } }
pub fn via_sub<T: Sub>(t: &T) { t.go(); t.sub(); }
pub fn via_where<T>(t: &T) where T: Other { t.go(); }
pub struct Holder<T>(pub T);
impl<T: Other> Holder<T> { pub fn run(&self) { self.0.go(); } }
pub fn assoc<I: Iterator<Item = S>>(it: I) { for s in it { s.hello(); } }
pub fn dyn_call(d: &dyn Sub) { d.go(); }
pub fn impl_arg(i: impl Other) { i.go(); }
fn main() { via_sub(&S); via_where(&S); Holder(S).run(); assoc(vec![S].into_iter()); dyn_call(&S); impl_arg(S); }
