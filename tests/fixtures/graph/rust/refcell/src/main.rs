use std::cell::RefCell;
use std::sync::Mutex;
pub struct S;
impl S {
    pub fn get_mut(&mut self) -> u8 { println!("S::get_mut (inherent)\n{}", std::backtrace::Backtrace::force_capture()); 0 }
    pub fn into_inner(self) -> u8 { println!("S::into_inner (inherent)\n{}", std::backtrace::Backtrace::force_capture()); 0 }
    pub fn replace(&self, _o: S) -> S { println!("S::replace (inherent)\n{}", std::backtrace::Backtrace::force_capture()); S }
}
pub fn by_mut_ref(c: &mut RefCell<S>) { c.get_mut(); }
pub fn by_value(mut c: RefCell<S>) { c.get_mut(); }
pub fn into_inner(c: RefCell<S>) { c.into_inner(); }
pub fn mutex_get_mut(m: &mut Mutex<S>) { let _ = m.get_mut(); }
pub fn replace(c: &RefCell<S>) { c.replace(S); }
fn main() {
    by_mut_ref(&mut RefCell::new(S));
    by_value(RefCell::new(S));
    into_inner(RefCell::new(S));
    mutex_get_mut(&mut Mutex::new(S));
    replace(&RefCell::new(S));
    println!("end: no S method ran\n{}", std::backtrace::Backtrace::force_capture());
}
