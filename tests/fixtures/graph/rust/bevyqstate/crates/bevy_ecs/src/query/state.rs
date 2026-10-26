use core::marker::PhantomData;

use super::fetch::{QueryData, ROQueryItem};
use super::filter::QueryFilter;
use super::{QueryEntityError, QuerySingleError};
use crate::entity::Entity;
use crate::world::World;

pub struct QueryState<D: QueryData, F: QueryFilter = ()> {
    marker: PhantomData<(D, F)>,
}

impl<D: QueryData, F: QueryFilter> QueryState<D, F> {
    pub fn new(_world: &World) -> Self {
        QueryState { marker: PhantomData }
    }

    pub fn get<'w>(&mut self, world: &'w World, entity: Entity) -> Result<ROQueryItem<'w, '_, D>, QueryEntityError> {
        <D::ReadOnly as QueryData>::fetch(world, entity.0).ok_or(QueryEntityError)
    }

    pub fn single_mut<'w>(&mut self, world: &'w mut World) -> Result<D::Item<'w, '_>, QuerySingleError> {
        D::fetch(world, 0).ok_or(QuerySingleError)
    }
}
