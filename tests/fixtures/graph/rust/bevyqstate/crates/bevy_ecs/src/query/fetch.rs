use core::marker::PhantomData;

use crate::change_detection::{Mut, Ref};
use crate::entity::Entity;
use crate::world::{Component, World};

pub trait QueryData {
    type ReadOnly: QueryData;
    type Item<'w, 's>;
    fn fetch<'w, 's>(world: &'w World, row: usize) -> Option<Self::Item<'w, 's>>;
}

pub type QueryItem<'w, 's, Q> = <Q as QueryData>::Item<'w, 's>;
pub type ROQueryItem<'w, 's, D> = QueryItem<'w, 's, <D as QueryData>::ReadOnly>;

impl<'a, T: Component> QueryData for &'a T {
    type ReadOnly = Self;
    type Item<'w, 's> = &'w T;
    fn fetch<'w, 's>(world: &'w World, row: usize) -> Option<Self::Item<'w, 's>> {
        // SAFETY: fixture world; components live as long as the world.
        world.column::<T>(row).map(|ptr| unsafe { &*ptr })
    }
}

impl<'a, T: Component> QueryData for &'a mut T {
    type ReadOnly = &'a T;
    type Item<'w, 's> = Mut<'w, T>;
    fn fetch<'w, 's>(world: &'w World, row: usize) -> Option<Self::Item<'w, 's>> {
        // SAFETY: fixture world; one query borrows a column at a time.
        world.column::<T>(row).map(|ptr| Mut { value: unsafe { &mut *ptr }, changed: false })
    }
}

impl<'a, T: Component> QueryData for Ref<'a, T> {
    type ReadOnly = Self;
    type Item<'w, 's> = Ref<'w, T>;
    fn fetch<'w, 's>(world: &'w World, row: usize) -> Option<Self::Item<'w, 's>> {
        // SAFETY: fixture world; components live as long as the world.
        world.column::<T>(row).map(|ptr| Ref { value: unsafe { &*ptr }, added: true })
    }
}

impl QueryData for Entity {
    type ReadOnly = Self;
    type Item<'w, 's> = Entity;
    fn fetch<'w, 's>(_world: &'w World, row: usize) -> Option<Self::Item<'w, 's>> {
        Some(Entity(row))
    }
}

pub struct Has<T>(PhantomData<T>);

impl<T: Component> QueryData for Has<T> {
    type ReadOnly = Self;
    type Item<'w, 's> = bool;
    fn fetch<'w, 's>(world: &'w World, row: usize) -> Option<Self::Item<'w, 's>> {
        Some(world.column::<T>(row).is_some())
    }
}

impl<D: QueryData> QueryData for Option<D> {
    type ReadOnly = Option<D::ReadOnly>;
    type Item<'w, 's> = Option<D::Item<'w, 's>>;
    fn fetch<'w, 's>(world: &'w World, row: usize) -> Option<Self::Item<'w, 's>> {
        Some(D::fetch(world, row))
    }
}

impl<A: QueryData, B: QueryData> QueryData for (A, B) {
    type ReadOnly = (A::ReadOnly, B::ReadOnly);
    type Item<'w, 's> = (A::Item<'w, 's>, B::Item<'w, 's>);
    fn fetch<'w, 's>(world: &'w World, row: usize) -> Option<Self::Item<'w, 's>> {
        Some((A::fetch(world, row)?, B::fetch(world, row)?))
    }
}

impl<A: QueryData, B: QueryData, C: QueryData> QueryData for (A, B, C) {
    type ReadOnly = (A::ReadOnly, B::ReadOnly, C::ReadOnly);
    type Item<'w, 's> = (A::Item<'w, 's>, B::Item<'w, 's>, C::Item<'w, 's>);
    fn fetch<'w, 's>(world: &'w World, row: usize) -> Option<Self::Item<'w, 's>> {
        Some((A::fetch(world, row)?, B::fetch(world, row)?, C::fetch(world, row)?))
    }
}

impl<'a> QueryData for crate::world::entity_access::entity_ref::EntityRef<'a> {
    type ReadOnly = Self;
    type Item<'w, 's> = crate::world::entity_access::entity_ref::EntityRef<'w>;
    fn fetch<'w, 's>(_world: &'w World, row: usize) -> Option<Self::Item<'w, 's>> {
        Some(crate::world::entity_access::entity_ref::EntityRef { row, marker: PhantomData })
    }
}

impl<'a> QueryData for crate::world::entity_access::entity_mut::EntityMut<'a> {
    type ReadOnly = crate::world::entity_access::entity_ref::EntityRef<'a>;
    type Item<'w, 's> = crate::world::entity_access::entity_mut::EntityMut<'w>;
    fn fetch<'w, 's>(_world: &'w World, row: usize) -> Option<Self::Item<'w, 's>> {
        Some(crate::world::entity_access::entity_mut::EntityMut { row, marker: PhantomData })
    }
}
