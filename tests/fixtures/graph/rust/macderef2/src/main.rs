mod wrap;

use core::ops::Deref;
use wrap::{deref_to_inner, deref_with_own_load};

pub struct Inner;

impl Inner {
    pub fn load(&self) -> u32 {
        println!("Inner::load\n{}", std::backtrace::Backtrace::force_capture());
        1
    }
}

pub struct Gated(Inner);

#[cfg(any())]
deref_to_inner!(Gated);

impl Deref for Gated {
    type Target = Inner;
    fn deref(&self) -> &Inner {
        &self.0
    }
}

pub struct Own(Inner);

deref_with_own_load!(Own);

fn gated(g: &Gated) -> u32 {
    g.load()
}

fn own(o: &Own) -> u32 {
    o.load()
}

fn main() {
    let total = gated(&Gated(Inner)) + own(&Own(Inner));
    println!("{}\n{}", total, std::backtrace::Backtrace::force_capture());
}
