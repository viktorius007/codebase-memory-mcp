pub trait Adapter {
    fn raw(&self) -> u32;
}

pub trait Store {
    fn create(&mut self) -> u32;
}

pub struct Canon<A> {
    adapter: A,
}

impl<A> Canon<A> {
    pub fn new(adapter: A) -> Self {
        Canon { adapter }
    }
}

pub struct Sql;

impl Adapter for Sql {
    fn raw(&self) -> u32 {
        1
    }
}
