mod sync {
    pub use implementation::{RwLock, RwLockReadGuard, RwLockWriteGuard};
    #[cfg(unix)]
    use std::sync as implementation;
    #[cfg(not(unix))]
    mod implementation {
        pub use std::sync::{RwLock, RwLockReadGuard, RwLockWriteGuard};
    }
}
use sync::{RwLock, RwLockReadGuard, RwLockWriteGuard};
#[derive(Default)]
pub struct Registry(u8);
impl Registry {
    pub fn register(&mut self) -> u8 { println!("Registry::register\n{}", std::backtrace::Backtrace::force_capture()); self.0 }
    pub fn get(&self) -> u8 { println!("Registry::get\n{}", std::backtrace::Backtrace::force_capture()); self.0 }
}
#[derive(Default)]
pub struct RegistryArc { inner: RwLock<Registry> }
impl RegistryArc {
    pub fn read(&self) -> RwLockReadGuard<'_, Registry> { self.inner.read().unwrap() }
    pub fn write(&self) -> RwLockWriteGuard<'_, Registry> { self.inner.write().unwrap() }
}
fn l_write(r: &RegistryArc) { let mut w = r.write(); w.register(); }
fn l_read(r: &RegistryArc) { r.read().get(); }
fn main() {
    let r = RegistryArc::default();
    l_write(&r); l_read(&r);
    println!("end\n{}", std::backtrace::Backtrace::force_capture());
}
