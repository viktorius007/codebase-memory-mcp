use std::marker::PhantomData;

pub struct Scope<'scope, T> {
    marker: PhantomData<(&'scope (), T)>,
}
impl<'scope, T: 'scope> Scope<'scope, T> {
    pub fn spawn(&self, value: T) {
        drop(value);
    }
}

pub struct Pool;
impl Pool {
    pub fn scope<F, T>(&self, f: F) -> usize
    where
        F: for<'scope> FnOnce(&'scope Scope<'scope, T>),
    {
        f(&Scope { marker: PhantomData });
        0
    }
}

pub fn run() -> usize {
    Pool.scope(|scope| {
        scope.spawn(1);
    })
}
