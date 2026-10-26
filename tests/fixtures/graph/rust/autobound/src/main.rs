use std::marker::PhantomData;

pub struct Scope<'scope, 'env: 'scope, T> {
    marker: PhantomData<(&'scope (), &'env (), T)>,
}
impl<'scope, 'env, T: Send + 'scope> Scope<'scope, 'env, T> {
    pub fn spawn(&self, value: T) { println!("Scope::spawn\n{}", std::backtrace::Backtrace::force_capture()); drop(value); }
}

pub struct Pool;
impl Pool {
    pub fn scope<'env, F, T>(&self, f: F) -> Vec<T>
    where
        F: for<'scope> FnOnce(&'scope Scope<'scope, 'env, T>),
        T: Send + 'static,
    {
        let scope = Scope { marker: PhantomData };
        f(&scope);
        Vec::new()
    }
}

pub struct Keyed<T: ?Sized> { marker: PhantomData<T> }
impl<T> Keyed<T> where T: Sync + ?Sized {
    pub fn key(&self) -> u8 { println!("Keyed::key\n{}", std::backtrace::Backtrace::force_capture()); 1 }
}
pub fn with_keyed<T: Sync, F: FnOnce(&Keyed<T>) -> u8>(f: F) -> u8 { f(&Keyed { marker: PhantomData }) }

fn main() {
    let pool = Pool;
    let out = pool.scope(|scope| {
        for i in 0..2u32 {
            scope.spawn(i);
        }
    });
    let k = with_keyed::<u32, _>(|keyed| keyed.key());
    let _ = (out, k);
}
