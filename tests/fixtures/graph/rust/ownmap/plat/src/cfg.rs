#[doc(hidden)]
#[macro_export]
macro_rules! enabled {
    ($($p:tt)*) => { $($p)* };
}
pub use crate::enabled as alloc;
