use std::ops::Deref;
// Copy supplied by a macro (as salsa / bitflags / newtype macros do).
macro_rules! copy_impl {
    ($t:ty) => {
        impl Copy for $t {}
        impl Clone for $t { fn clone(&self) -> Self { *self } }
    };
}
pub struct E;
impl E { pub fn file(&self) -> u8 { println!("E::file inherent (&self)\n{}", std::backtrace::Backtrace::force_capture()); 2 } }

pub struct K;
copy_impl!(K);
impl K { pub fn file(self) -> u8 { println!("K::file inherent (self)\n{}", std::backtrace::Backtrace::force_capture()); 1 } }
pub fn by_ref(k: &K) -> u8 { k.file() }

pub struct KD;
copy_impl!(KD);
impl KD { pub fn file(self) -> u8 { println!("KD::file inherent (self)\n{}", std::backtrace::Backtrace::force_capture()); 3 } }
impl Deref for KD { type Target = E; fn deref(&self) -> &E { &E } }
pub fn by_ref_deref(k: &KD) -> u8 { k.file() }

#[derive(Clone, Copy)]
pub struct V;
impl V { pub fn file(self) -> u8 { println!("V::file inherent (self)\n{}", std::backtrace::Backtrace::force_capture()); 4 } }
impl Deref for V { type Target = E; fn deref(&self) -> &E { &E } }
pub fn by_ref_visible(v: &V) -> u8 { v.file() }

pub struct M;
impl Copy for M {}
impl Clone for M { fn clone(&self) -> Self { *self } }
impl M { pub fn file(self) -> u8 { println!("M::file inherent (self)\n{}", std::backtrace::Backtrace::force_capture()); 5 } }
pub fn by_ref_manual(m: &M) -> u8 { m.file() }
fn main() { by_ref(&K); by_ref_deref(&KD); by_ref_visible(&V); by_ref_manual(&M); }
