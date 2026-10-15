use std::rc::Rc;
use std::sync::Arc;
pub trait Ext { fn frob(&self); }
pub struct S;
impl Ext for S { fn frob(&self) { println!("<S as Ext>::frob\n{}", std::backtrace::Backtrace::force_capture()); } }
impl<T: Ext + ?Sized> Ext for Box<T> { fn frob(&self) { println!("<Box<T> as Ext>::frob\n{}", std::backtrace::Backtrace::force_capture()); (**self).frob() } }
impl<T: Ext + ?Sized> Ext for Rc<T> { fn frob(&self) { println!("<Rc<T> as Ext>::frob\n{}", std::backtrace::Backtrace::force_capture()); (**self).frob() } }
impl<'a, T: Ext + ?Sized> Ext for &'a T { fn frob(&self) { println!("<&T as Ext>::frob\n{}", std::backtrace::Backtrace::force_capture()); (**self).frob() } }
pub trait Ext2 { fn zap(&self); }
impl Ext2 for S { fn zap(&self) { println!("<S as Ext2>::zap\n{}", std::backtrace::Backtrace::force_capture()); } }
impl<T: Ext2 + ?Sized> Ext2 for Arc<T> { fn zap(&self) { println!("<Arc<T> as Ext2>::zap\n{}", std::backtrace::Backtrace::force_capture()); } }
pub fn on_box(b: Box<S>) { b.frob(); }
pub fn on_rc(r: Rc<S>) { r.frob(); }
pub fn on_ref(r: &S) { r.frob(); }
pub fn on_refref(r: &&S) { r.frob(); }
pub fn on_arc(a: Arc<S>) { a.zap(); }
pub fn on_box_dyn(b: Box<dyn Ext>) { b.frob(); }
fn main() { println!("-- box\n{}", std::backtrace::Backtrace::force_capture()); on_box(Box::new(S)); println!("-- rc\n{}", std::backtrace::Backtrace::force_capture()); on_rc(Rc::new(S)); println!("-- ref\n{}", std::backtrace::Backtrace::force_capture()); on_ref(&S); println!("-- refref\n{}", std::backtrace::Backtrace::force_capture()); on_refref(&&S); println!("-- arc\n{}", std::backtrace::Backtrace::force_capture()); on_arc(Arc::new(S)); println!("-- boxdyn\n{}", std::backtrace::Backtrace::force_capture()); on_box_dyn(Box::new(S)); }
