use std::rc::Rc;
use std::sync::Arc;
use std::cell::RefCell;
pub struct S;
impl S {
    pub fn clone(&self) -> S { println!("S::clone (inherent)\n{}", std::backtrace::Backtrace::force_capture()); S }
    pub fn as_ref(&self) -> &S { println!("S::as_ref (inherent)\n{}", std::backtrace::Backtrace::force_capture()); self }
    pub fn borrow(&self) -> u8 { println!("S::borrow (inherent)\n{}", std::backtrace::Backtrace::force_capture()); 0 }
    pub fn get_mut(&mut self) -> u8 { println!("S::get_mut (inherent)\n{}", std::backtrace::Backtrace::force_capture()); 0 }
    pub fn into_inner(self) -> u8 { println!("S::into_inner (inherent)\n{}", std::backtrace::Backtrace::force_capture()); 0 }
    pub fn take(&mut self) -> u8 { println!("S::take (inherent)\n{}", std::backtrace::Backtrace::force_capture()); 0 }
    pub fn len(&self) -> usize { println!("S::len (inherent)\n{}", std::backtrace::Backtrace::force_capture()); 0 }
}
pub fn rc_clone(r: Rc<S>) { let _x = r.clone(); }
pub fn arc_clone(a: Arc<S>) { let _x = a.clone(); }
pub fn box_as_ref(b: Box<S>) { let _x: &S = b.as_ref(); }
pub fn rc_as_ref(r: Rc<S>) { let _x: &S = r.as_ref(); }
pub fn box_clone(b: Box<S>) { let _x = b.clone(); }
pub fn rc_get_mut(mut r: Box<S>) { let _x = r.get_mut(); }
pub fn refcell_borrow(c: RefCell<S>) { let _x = c.borrow(); }
pub fn refcell_take(c: &mut RefCell<S>) { c.get_mut(); }
pub fn vec_len(v: Vec<S>) { let _ = v.len(); }
fn main() {
    rc_clone(Rc::new(S));
    arc_clone(Arc::new(S));
    box_as_ref(Box::new(S));
    rc_as_ref(Rc::new(S));
    box_clone(Box::new(S));
    rc_get_mut(Box::new(S));
    refcell_borrow(RefCell::new(S));
    refcell_take(&mut RefCell::new(S));
    vec_len(vec![]);
    println!("end\n{}", std::backtrace::Backtrace::force_capture());
}
