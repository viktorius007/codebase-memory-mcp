//! A crate whose exported macros call each other through `$crate`.
pub mod spawn {
    pub struct Spawn<T>(pub T);
    pub trait SpawnRelated {
        fn spawn<L>(list: L) -> L {
            list
        }
    }
}

pub mod hierarchy {
    pub struct Children;
    impl crate::spawn::SpawnRelated for Children {}
}

#[macro_export]
macro_rules! related {
    ($t:ty [$($child:expr),*$(,)?]) => {
        <$t as $crate::spawn::SpawnRelated>::spawn($crate::recursive_spawn!($($child),*))
    };
}

#[macro_export]
#[doc(hidden)]
macro_rules! recursive_spawn {
    () => { () };
    ($a:expr) => { $crate::spawn::Spawn($a) };
    ($a:expr, $b:expr) => { ($crate::spawn::Spawn($a), $crate::spawn::Spawn($b)) };
}

#[macro_export]
macro_rules! children {
    [$($child:expr),*$(,)?] => {
        $crate::related!($crate::hierarchy::Children [$($child),*])
    };
}

pub mod prelude {
    pub use crate::{children, spawn::Spawn};
}
