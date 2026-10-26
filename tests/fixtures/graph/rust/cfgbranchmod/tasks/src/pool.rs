use std::cell::Cell;
use std::marker::PhantomData;

pub struct Scope<'scope, T> {
    count: Cell<usize>,
    marker: PhantomData<(&'scope (), T)>,
}
impl<'scope, T: Send + 'scope> Scope<'scope, T> {
    pub fn spawn(&self, value: T) {
        println!("pool::Scope::spawn\n{}", std::backtrace::Backtrace::force_capture());
        drop(value);
        self.count.set(self.count.get() + 1);
    }
}

pub struct Pool;
impl Pool {
    pub fn scope<F, T>(&self, f: F) -> usize
    where
        F: for<'scope> FnOnce(&'scope Scope<'scope, T>),
        T: Send + 'static,
    {
        let scope = Scope { count: Cell::new(0), marker: PhantomData };
        f(&scope);
        scope.count.get()
    }
}

pub fn run() -> usize {
    Pool.scope(|scope| {
        scope.spawn(1);
    })
}
