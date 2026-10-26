macro_rules! deref_to_inner {
    ($name:ident) => {
        impl Deref for $name {
            type Target = Elsewhere;
            fn deref(&self) -> &Elsewhere {
                &ELSEWHERE
            }
        }
    };
}

macro_rules! deref_with_own_load {
    ($name:ident) => {
        impl $name {
            pub fn load(&self) -> u32 {
                println!("{}::load\n{}", stringify!($name), std::backtrace::Backtrace::force_capture());
                3
            }
        }
        impl Deref for $name {
            type Target = Inner;
            fn deref(&self) -> &Inner {
                &self.0
            }
        }
    };
}

pub(crate) use deref_to_inner;
pub(crate) use deref_with_own_load;
