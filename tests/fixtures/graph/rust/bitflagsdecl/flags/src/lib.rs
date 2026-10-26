/// A stand-in for the bitflags crate: declares the struct and its consts.
#[macro_export]
macro_rules! bitflags {
    ($(#[$m:meta])* $vis:vis struct $name:ident: $ty:ty { $(const $flag:ident = $value:expr;)* }) => {
        $(#[$m])*
        #[derive(Clone, Copy, PartialEq, Eq)]
        $vis struct $name($ty);
        impl $name {
            $(pub const $flag: Self = Self($value);)*
            pub fn bits(&self) -> $ty {
                self.0
            }
        }
    };
}
