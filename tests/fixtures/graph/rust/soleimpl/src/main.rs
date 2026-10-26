mod dflt;
mod facade;
mod store;

use dflt::{Bad, Pick, Wrap};
use store::{Canon, Sql, Store};

fn run() -> u32 {
    let mut store = Canon::new(Sql);
    store.create()
}

fn inherited() -> u32 {
    let bad = Wrap { inner: Bad };
    bad.pick()
}

fn main() {
    let good = dflt::Wrap { inner: dflt::Good };
    let _ = good.pick();
    let _ = inherited();
    let store = Canon::new(Sql);
    let _ = facade::touch(&store);
    println!("{}\n{}", run(), std::backtrace::Backtrace::force_capture());
}
