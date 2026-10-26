plat::multi_threaded! {
    if {
        mod pool;
        use pool::run;
    } else {
        mod single;
        use single::run;
    }
}

fn main() {
    let _ = run();
}
