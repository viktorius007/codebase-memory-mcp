use std::pin::Pin;
use std::rc::Rc;
use std::sync::{Arc, Mutex};
pub struct S;
impl S {
    pub fn set(&mut self, _v: S) { println!("S::set ran\n{}", std::backtrace::Backtrace::force_capture()); }
    pub fn as_mut(&mut self) -> &mut S { println!("S::as_mut ran\n{}", std::backtrace::Backtrace::force_capture()); self }
    pub fn get_ref(&self) -> &S { println!("S::get_ref ran\n{}", std::backtrace::Backtrace::force_capture()); self }
    pub fn as_ref(&self) -> &S { println!("S::as_ref ran\n{}", std::backtrace::Backtrace::force_capture()); self }
    pub fn borrow(&self) -> &S { println!("S::borrow ran\n{}", std::backtrace::Backtrace::force_capture()); self }
    pub fn lock(&self) -> u8 { println!("S::lock ran\n{}", std::backtrace::Backtrace::force_capture()); 0 }
    pub fn is_poisoned(&self) -> bool { println!("S::is_poisoned ran\n{}", std::backtrace::Backtrace::force_capture()); false }
    pub fn get_mut(&mut self) -> u8 { println!("S::get_mut ran\n{}", std::backtrace::Backtrace::force_capture()); 0 }
}
pub fn pin_set(mut p: Pin<&mut S>) { p.set(S); }
pub fn pin_as_mut(mut p: Pin<&mut S>) { let _ = p.as_mut(); }
pub fn pin_get_ref(p: Pin<&S>) { let _ = p.get_ref(); }
pub fn pin_as_ref(p: Pin<&mut S>) { let _ = p.as_ref(); }
pub fn pin_get_mut(p: Pin<&mut S>) { let _ = p.get_mut(); }
pub fn arc_mutex_lock(a: Arc<Mutex<S>>) { let _g = a.lock(); }
pub fn arc_mutex_poison(a: Arc<Mutex<S>>) { let _ = a.is_poisoned(); }
pub fn guard_lock(m: &Mutex<S>) { let g = m.lock().unwrap(); g.lock(); }
pub fn rc_as_ref(r: Rc<S>) { let _x: &S = r.as_ref(); }
fn main() {
    let mut s = S; pin_set(Pin::new(&mut s));
    let mut s = S; pin_as_mut(Pin::new(&mut s));
    let s = S; pin_get_ref(Pin::new(&s));
    let mut s = S; pin_as_ref(Pin::new(&mut s));
    let mut s = S; pin_get_mut(Pin::new(&mut s));
    arc_mutex_lock(Arc::new(Mutex::new(S)));
    arc_mutex_poison(Arc::new(Mutex::new(S)));
    guard_lock(&Mutex::new(S));
    rc_as_ref(Rc::new(S));
    println!("end\n{}", std::backtrace::Backtrace::force_capture());
}
