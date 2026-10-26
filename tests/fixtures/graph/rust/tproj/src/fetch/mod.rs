#[derive(Clone, Copy)]
pub struct Entity(pub u32);
pub struct EntityRef<'w>(pub &'w World, pub u32);
pub struct EntityWorldMut<'w>(pub &'w mut World, pub u32);
pub struct World {
    pub count: u32,
}

pub trait WorldEntityFetch {
    type Ref<'w>;
    type Mut<'w>;
    fn fetch_ref(self, world: &World) -> Self::Ref<'_>;
    fn fetch_mut(self, world: &mut World) -> Self::Mut<'_>;
}

impl WorldEntityFetch for Entity {
    type Ref<'w> = EntityRef<'w>;
    type Mut<'w> = EntityWorldMut<'w>;
    fn fetch_ref(self, world: &World) -> Self::Ref<'_> {
        EntityRef(world, self.0)
    }
    fn fetch_mut(self, world: &mut World) -> Self::Mut<'_> {
        EntityWorldMut(world, self.0)
    }
}

impl<'w> EntityRef<'w> {
    pub fn id(&self) -> u32 {
        println!("EntityRef::id\n{}", std::backtrace::Backtrace::force_capture());
        self.1 + self.0.count
    }
}

impl<'w> EntityWorldMut<'w> {
    pub fn insert(&mut self, v: u32) -> u32 {
        println!("EntityWorldMut::insert\n{}", std::backtrace::Backtrace::force_capture());
        self.0.count += v;
        self.1
    }
}

impl World {
    pub fn entity<F: WorldEntityFetch>(&self, e: F) -> F::Ref<'_> {
        e.fetch_ref(self)
    }
    pub fn entity_mut<F: WorldEntityFetch>(&mut self, e: F) -> F::Mut<'_> {
        e.fetch_mut(self)
    }
    pub fn get_entity_mut<F: WorldEntityFetch>(&mut self, e: F) -> Result<F::Mut<'_>, ()> {
        Ok(e.fetch_mut(self))
    }
    pub fn spawn_one<F: WorldEntityFetch + From<u32>>(&mut self) -> F::Mut<'_> {
        F::from(self.count).fetch_mut(self)
    }
}

impl From<u32> for Entity {
    fn from(v: u32) -> Self {
        Entity(v)
    }
}
