use core::marker::PhantomData;

use crate::entity::Entity;
use crate::query::{
    QueryData, QueryEntityError, QueryFilter, QueryIter, QuerySingleError, ROQueryItem,
};
use crate::world::World;

pub struct Query<'w, 's, D: QueryData, F: QueryFilter = ()> {
    world: &'w World,
    marker: PhantomData<(&'s (), D, F)>,
}

impl<'w, 's, D: QueryData, F: QueryFilter> Query<'w, 's, D, F> {
    pub fn new(world: &'w World) -> Self {
        Query { world, marker: PhantomData }
    }

    pub fn iter(&self) -> QueryIter<'_, 's, D::ReadOnly, F> {
        QueryIter { world: self.world, row: 0, marker: PhantomData }
    }

    pub fn iter_mut(&mut self) -> QueryIter<'_, 's, D, F> {
        QueryIter { world: self.world, row: 0, marker: PhantomData }
    }

    pub fn get(&self, entity: Entity) -> Result<ROQueryItem<'_, 's, D>, QueryEntityError> {
        <D::ReadOnly as QueryData>::fetch(self.world, entity.0).ok_or(QueryEntityError)
    }

    pub fn get_mut(&mut self, entity: Entity) -> Result<D::Item<'_, 's>, QueryEntityError> {
        D::fetch(self.world, entity.0).ok_or(QueryEntityError)
    }

    pub fn single(&self) -> Result<ROQueryItem<'_, 's, D>, QuerySingleError> {
        self.iter().next().ok_or(QuerySingleError)
    }

    pub fn single_mut(&mut self) -> Result<D::Item<'_, 's>, QuerySingleError> {
        self.iter_mut().next().ok_or(QuerySingleError)
    }
}

impl<'w, 's, D: QueryData, F: QueryFilter> IntoIterator for &'w Query<'_, 's, D, F> {
    type Item = ROQueryItem<'w, 's, D>;
    type IntoIter = QueryIter<'w, 's, D::ReadOnly, F>;

    fn into_iter(self) -> Self::IntoIter {
        self.iter()
    }
}

impl<'w, 's, D: QueryData, F: QueryFilter> IntoIterator for &'w mut Query<'_, 's, D, F> {
    type Item = D::Item<'w, 's>;
    type IntoIter = QueryIter<'w, 's, D, F>;

    fn into_iter(self) -> Self::IntoIter {
        self.iter_mut()
    }
}
