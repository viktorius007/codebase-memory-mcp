use core::any::{Any, TypeId};
use std::collections::HashMap;

use crate::entity::Entity;

pub trait Component: Any {}

#[derive(Default)]
pub struct World {
    pub(crate) rows: Vec<HashMap<TypeId, *mut dyn Any>>,
}

impl World {
    pub fn spawn(&mut self, parts: Vec<(TypeId, Box<dyn Any>)>) -> Entity {
        let mut row = HashMap::new();
        for (id, part) in parts {
            row.insert(id, Box::into_raw(part));
        }
        self.rows.push(row);
        Entity(self.rows.len() - 1)
    }

    pub(crate) fn column<T: Component>(&self, row: usize) -> Option<*mut T> {
        let ptr = *self.rows[row].get(&TypeId::of::<T>())?;
        // SAFETY: the pointer came from Box::into_raw of a value of type T and is never freed.
        unsafe { (*ptr).downcast_mut::<T>().map(|value| value as *mut T) }
    }
}
pub mod entity_access;
