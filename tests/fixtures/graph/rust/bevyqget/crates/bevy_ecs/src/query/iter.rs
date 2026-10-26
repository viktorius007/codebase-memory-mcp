use core::marker::PhantomData;

use super::fetch::QueryData;
use super::filter::QueryFilter;
use crate::world::World;

pub struct QueryIter<'w, 's, D: QueryData, F: QueryFilter> {
    pub(crate) world: &'w World,
    pub(crate) row: usize,
    pub(crate) marker: PhantomData<(&'s (), D, F)>,
}

impl<'w, 's, D: QueryData, F: QueryFilter> Iterator for QueryIter<'w, 's, D, F> {
    type Item = D::Item<'w, 's>;

    fn next(&mut self) -> Option<Self::Item> {
        while self.row < self.world.rows.len() {
            let row = self.row;
            self.row += 1;
            if F::matches(self.world, row) {
                if let Some(item) = D::fetch(self.world, row) {
                    return Some(item);
                }
            }
        }
        None
    }
}
