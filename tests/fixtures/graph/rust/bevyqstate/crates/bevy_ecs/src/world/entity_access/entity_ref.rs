use core::marker::PhantomData;

use crate::entity::Entity;

pub struct EntityRef<'w> {
    pub(crate) row: usize,
    pub(crate) marker: PhantomData<&'w ()>,
}

impl<'w> EntityRef<'w> {
    pub fn id(&self) -> Entity {
        println!("EntityRef::id\n{}", std::backtrace::Backtrace::force_capture());
        Entity(self.row)
    }
}
