use std::marker::PhantomData;

pub trait CreateTypeData<T, Input = ()> {
    fn create_type_data(input: Input) -> Self;
}

pub struct ReflectState(u8);

impl<S: Default> CreateTypeData<S, ()> for ReflectState {
    fn create_type_data(_input: ()) -> Self {
        println!("ReflectState::create_type_data\n{}", std::backtrace::Backtrace::force_capture());
        ReflectState(0)
    }
}

pub trait Query {
    fn init_fetch(n: u8) -> u8;
}

pub struct Sub<L>(PhantomData<L>);

impl<T> Query for &T {
    fn init_fetch(n: u8) -> u8 {
        println!("&T::init_fetch\n{}", std::backtrace::Backtrace::force_capture());
        n
    }
}

pub trait Tr<A, B = ()> {
    fn f() -> u8 {
        println!("Tr::f\n{}", std::backtrace::Backtrace::force_capture());
        0
    }
}

pub struct S;

impl Tr<u8> for S {}

impl Tr<u8, u16> for S {
    fn f() -> u8 {
        println!("S::f\n{}", std::backtrace::Backtrace::force_capture());
        1
    }
}

fn inherit() -> u8 {
    <S as Tr<u8>>::f()
}

fn make<T: Default>() -> u8 {
    <ReflectState as CreateTypeData<T>>::create_type_data(()).0
}

fn fetch<L>() -> u8 {
    <&Sub<L> as Query>::init_fetch(1)
}

fn main() {
    make::<u8>();
    inherit();
    fetch::<u8>();
    println!("end\n{}", std::backtrace::Backtrace::force_capture());
}
