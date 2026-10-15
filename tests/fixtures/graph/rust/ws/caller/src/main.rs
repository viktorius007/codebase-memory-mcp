use dep::{OwnTrait, StoreTrait};
pub fn run(store: &dep::Store) { store.act(); }
pub fn run_own() { let o = dep::Own; o.go(); }
fn main() { run(&dep::Store); run_own(); }
