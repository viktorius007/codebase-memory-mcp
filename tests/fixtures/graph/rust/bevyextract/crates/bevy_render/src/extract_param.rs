use core::ops::Deref;

use bevy_ecs::system::{SystemParam, SystemParamItem};

pub struct Extract<'w, 's, P: SystemParam + 'static> {
    item: SystemParamItem<'w, 's, P>,
}

impl<'w, 's, P: SystemParam> Extract<'w, 's, P> {
    pub fn new(item: SystemParamItem<'w, 's, P>) -> Self {
        Extract { item }
    }
}

impl<'w, 's, P: SystemParam> Deref for Extract<'w, 's, P> {
    type Target = SystemParamItem<'w, 's, P>;

    fn deref(&self) -> &Self::Target {
        &self.item
    }
}

impl<'a, 'w, 's, P: SystemParam> IntoIterator for &'a Extract<'w, 's, P>
where
    &'a SystemParamItem<'w, 's, P>: IntoIterator,
{
    type Item = <&'a SystemParamItem<'w, 's, P> as IntoIterator>::Item;
    type IntoIter = <&'a SystemParamItem<'w, 's, P> as IntoIterator>::IntoIter;

    fn into_iter(self) -> Self::IntoIter {
        (&self.item).into_iter()
    }
}
