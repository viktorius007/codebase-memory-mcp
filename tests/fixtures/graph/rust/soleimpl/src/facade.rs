use crate::store::{Adapter, Canon, Store};

impl<A: Adapter> Store for Canon<A> {
    fn create(&mut self) -> u32 {
        println!("Canon::create\n{}", std::backtrace::Backtrace::force_capture());
        7
    }
}

pub fn touch<A: Adapter>(c: &Canon<A>) -> bool {
    let _ = c;
    true
}
