pub trait T {}
impl T for () {}
impl T for fn(usize) -> bool {}
pub fn f() where (): T, fn(usize) -> bool: T {}
pub fn after() {}
