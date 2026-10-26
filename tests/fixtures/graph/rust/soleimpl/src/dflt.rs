pub trait Bound {}

pub trait Pick: Sized {
    fn pick(self) -> u32 {
        println!("Pick::pick default\n{}", std::backtrace::Backtrace::force_capture());
        0
    }
}

pub struct Wrap<A> {
    pub inner: A,
}

pub struct Good;

pub struct Bad;

impl Bound for Good {}

impl<A: Bound> Pick for Wrap<A> {
    fn pick(self) -> u32 {
        println!("Wrap::pick\n{}", std::backtrace::Backtrace::force_capture());
        1
    }
}

impl Pick for &Wrap<Bad> {}
