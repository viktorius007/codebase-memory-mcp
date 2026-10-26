use core::marker::PhantomData;

use crate::world::{Component, World};

pub trait QueryFilter {
    fn matches(world: &World, row: usize) -> bool;
}

impl QueryFilter for () {
    fn matches(_world: &World, _row: usize) -> bool {
        true
    }
}

pub struct With<T>(PhantomData<T>);

impl<T: Component> QueryFilter for With<T> {
    fn matches(world: &World, row: usize) -> bool {
        world.column::<T>(row).is_some()
    }
}
