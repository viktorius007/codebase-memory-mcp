use core::ops::Deref;
use super::traits::change_detection_impl;
use super::Resource;
pub struct Mutable;
pub struct Res<'w, T: ?Sized + Resource> { pub value: &'w T }
pub struct ResMut<'w, T: ?Sized + Resource> { pub value: &'w T }
change_detection_impl!(Res<'w, T>, T, Resource);
change_detection_impl!(ResMut<'w, T>, T, Resource<Mutability = Mutable>);
