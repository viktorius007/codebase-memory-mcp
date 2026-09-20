pub mod contest;
pub mod latest;
pub mod testbed;

use alpha::{Greeter, Loud, add};

pub fn total() -> i32 {
    add(1, 2) + local_helper()
}

fn local_helper() -> i32 {
    3
}

pub fn announce() -> String {
    let l = Loud;
    l.greet()
}

pub fn shadowed(local_helper: impl Fn() -> i32) -> i32 {
    local_helper()
}
