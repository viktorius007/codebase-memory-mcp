mod map;
mod wrap;

use map::{Map, Phases};
use wrap::Local;

fn system(phases: Local<Phases>) -> usize {
    phases.count()
}

fn main() {
    let owned: Phases = Map { keys: vec![1], values: vec![2] };
    let n = system(Local { value: &owned });
    println!("{}\n{}", n, std::backtrace::Backtrace::force_capture());
}
