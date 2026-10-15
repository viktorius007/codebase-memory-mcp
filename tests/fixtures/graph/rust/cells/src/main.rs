use std::cell::{Cell, RefCell};
#[derive(Clone, Copy, Default)] pub struct S;
impl S {
    pub fn as_ptr(&mut self) -> u8 { println!("S::as_ptr ran\n{}", std::backtrace::Backtrace::force_capture()); 0 }
    pub fn as_slice_of_cells(&mut self) -> u8 { println!("S::as_slice_of_cells ran\n{}", std::backtrace::Backtrace::force_capture()); 0 }
    pub fn borrow(&mut self) -> u8 { println!("S::borrow ran\n{}", std::backtrace::Backtrace::force_capture()); 0 }
    pub fn borrow_mut(&mut self) -> u8 { println!("S::borrow_mut ran\n{}", std::backtrace::Backtrace::force_capture()); 0 }
    pub fn clone(&mut self) -> u8 { println!("S::clone ran\n{}", std::backtrace::Backtrace::force_capture()); 0 }
    pub fn get(&mut self) -> u8 { println!("S::get ran\n{}", std::backtrace::Backtrace::force_capture()); 0 }
    pub fn get_mut(&mut self) -> u8 { println!("S::get_mut ran\n{}", std::backtrace::Backtrace::force_capture()); 0 }
    pub fn into_inner(&mut self) -> u8 { println!("S::into_inner ran\n{}", std::backtrace::Backtrace::force_capture()); 0 }
    pub fn replace(&mut self) -> u8 { println!("S::replace ran\n{}", std::backtrace::Backtrace::force_capture()); 0 }
    pub fn replace_with(&mut self) -> u8 { println!("S::replace_with ran\n{}", std::backtrace::Backtrace::force_capture()); 0 }
    pub fn set(&mut self) -> u8 { println!("S::set ran\n{}", std::backtrace::Backtrace::force_capture()); 0 }
    pub fn swap(&mut self) -> u8 { println!("S::swap ran\n{}", std::backtrace::Backtrace::force_capture()); 0 }
    pub fn take(&mut self) -> u8 { println!("S::take ran\n{}", std::backtrace::Backtrace::force_capture()); 0 }
    pub fn try_borrow(&mut self) -> u8 { println!("S::try_borrow ran\n{}", std::backtrace::Backtrace::force_capture()); 0 }
    pub fn try_borrow_mut(&mut self) -> u8 { println!("S::try_borrow_mut ran\n{}", std::backtrace::Backtrace::force_capture()); 0 }
    pub fn try_borrow_unguarded(&mut self) -> u8 { println!("S::try_borrow_unguarded ran\n{}", std::backtrace::Backtrace::force_capture()); 0 }
    pub fn update(&mut self) -> u8 { println!("S::update ran\n{}", std::backtrace::Backtrace::force_capture()); 0 }
}
pub fn refcell_get_mut(mut c: RefCell<S>) { c.get_mut(); }
pub fn refcell_into_inner(mut c: RefCell<S>) { c.into_inner(); }
pub fn refcell_replace(mut c: RefCell<S>) { c.replace(S); }
pub fn refcell_replace_with(mut c: RefCell<S>) { c.replace_with(|_| S); }
pub fn refcell_swap(mut c: RefCell<S>) { let o = RefCell::new(S); c.swap(&o); }
pub fn refcell_take(mut c: RefCell<S>) { c.take(); }
pub fn refcell_borrow(mut c: RefCell<S>) { let _ = c.borrow(); }
pub fn refcell_borrow_mut(mut c: RefCell<S>) { let _ = c.borrow_mut(); }
pub fn refcell_try_borrow(mut c: RefCell<S>) { let _ = c.try_borrow(); }
pub fn refcell_try_borrow_mut(mut c: RefCell<S>) { let _ = c.try_borrow_mut(); }
pub fn refcell_as_ptr(mut c: RefCell<S>) { c.as_ptr(); }
pub fn refcell_clone(mut c: RefCell<S>) { let _ = c.clone(); }
pub fn refcell_try_borrow_unguarded(mut c: RefCell<S>) { let _ = unsafe { c.try_borrow_unguarded() }; }
pub fn cell_get_mut(mut c: Cell<S>) { c.get_mut(); }
pub fn cell_into_inner(mut c: Cell<S>) { c.into_inner(); }
pub fn cell_replace(mut c: Cell<S>) { c.replace(S); }
pub fn cell_swap(mut c: Cell<S>) { let o = Cell::new(S); c.swap(&o); }
pub fn cell_take(mut c: Cell<S>) { c.take(); }
pub fn cell_get(mut c: Cell<S>) { c.get(); }
pub fn cell_set(mut c: Cell<S>) { c.set(S); }
pub fn cell_as_ptr(mut c: Cell<S>) { c.as_ptr(); }
pub fn cell_update(mut c: Cell<S>) { c.update(|x| x); }
pub fn cell_clone(mut c: Cell<S>) { let _ = c.clone(); }
fn main() { refcell_get_mut(RefCell::new(S)); refcell_try_borrow_unguarded(RefCell::new(S)); cell_get_mut(Cell::new(S)); refcell_into_inner(RefCell::new(S)); cell_take(Cell::new(S)); println!("end\n{}", std::backtrace::Backtrace::force_capture()); }
