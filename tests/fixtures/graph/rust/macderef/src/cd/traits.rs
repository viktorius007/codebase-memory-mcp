macro_rules! change_detection_impl {
    ($name:ident < $( $generics:tt ),+ >, $target:ty, $($traits:path)?) => {
        impl<$($generics),*: ?Sized $(+ $traits)?> Deref for $name<$($generics),*> {
            type Target = $target;
            fn deref(&self) -> &Self::Target { self.value }
        }
    }
}
pub(crate) use change_detection_impl;
