use core::marker::PhantomData;

pub trait Asset: 'static {}

pub enum Handle<A: Asset> {
    Strong(u32),
    Uuid(u64, PhantomData<A>),
}

impl<T: Asset> Clone for Handle<T> {
    fn clone(&self) -> Self {
        println!("Handle::clone\n{}", std::backtrace::Backtrace::force_capture());
        match self {
            Handle::Strong(id) => Handle::Strong(*id),
            Handle::Uuid(id, ..) => Handle::Uuid(*id, PhantomData),
        }
    }
}

impl<A: Asset> Handle<A> {
    pub fn id(&self) -> u64 {
        match self {
            Handle::Strong(id) => *id as u64,
            Handle::Uuid(id, ..) => *id,
        }
    }
}

pub struct Slot<T: Asset> {
    pub value: Option<T>,
}

impl<T: Asset + Default> Clone for Slot<T> {
    fn clone(&self) -> Self {
        println!("Slot::clone\n{}", std::backtrace::Backtrace::force_capture());
        Slot { value: Some(T::default()) }
    }
}

pub struct Pinned<'a, T: Asset> {
    pub item: &'a T,
}

impl<'a: 'static, T: Asset> Clone for Pinned<'a, T> {
    fn clone(&self) -> Self {
        println!("Pinned::clone\n{}", std::backtrace::Backtrace::force_capture());
        Pinned { item: self.item }
    }
}
