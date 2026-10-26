#[doc(hidden)]
#[macro_export]
macro_rules! enabled {
    ($($p:tt)*) => { $($p)* };
}
#[doc(hidden)]
#[macro_export]
macro_rules! disabled {
    ($($p:tt)*) => {};
}
pub use crate::enabled as alloc;
