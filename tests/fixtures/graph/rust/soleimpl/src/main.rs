mod facade;
mod store;

use store::{Canon, Sql, Store};

fn run() -> u32 {
    let mut store = Canon::new(Sql);
    store.create()
}

fn main() {
    let store = Canon::new(Sql);
    let _ = facade::touch(&store);
    println!("{}\n{}", run(), std::backtrace::Backtrace::force_capture());
}
