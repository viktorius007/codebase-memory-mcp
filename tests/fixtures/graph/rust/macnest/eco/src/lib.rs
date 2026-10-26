//! The macro chain of bevy_ecs's hierarchy.rs and spawn.rs.

pub mod spawn {
    pub struct Spawn<B>(pub B);

    pub trait SpawnRelated {
        fn spawn<L>(list: L) -> Vec<String>;
    }
}

pub mod hierarchy {
    pub struct Children;

    impl crate::spawn::SpawnRelated for Children {
        fn spawn<L>(_list: L) -> Vec<String> { Vec::new() }
    }
}

#[macro_export]
macro_rules! related {
    ($relationship_target:ty [$($child:expr),*$(,)?]) => {
       <$relationship_target as $crate::spawn::SpawnRelated>::spawn($crate::recursive_spawn!($($child),*))
    };
}

#[macro_export]
#[doc(hidden)]
macro_rules! recursive_spawn {
    () => { () };
    ($a:expr) => {
        $crate::spawn::Spawn($a)
    };
    ($a:expr, $b:expr) => {
        (
            $crate::spawn::Spawn($a),
            $crate::spawn::Spawn($b),
        )
    };
}

#[macro_export]
macro_rules! children {
    [$($child:expr),*$(,)?] => {
        $crate::related!($crate::hierarchy::Children [$($child),*])
    };
}
