use core::marker::PhantomData;

use crate::entity::Entity;

pub struct EntityMut<'w> {
    pub(crate) row: usize,
    pub(crate) marker: PhantomData<&'w mut ()>,
}

impl<'w> EntityMut<'w> {
    pub fn despawn_marker(&mut self) -> Entity {
        println!("EntityMut::despawn_marker\n{}", std::backtrace::Backtrace::force_capture());
        Entity(self.row)
    }
}
