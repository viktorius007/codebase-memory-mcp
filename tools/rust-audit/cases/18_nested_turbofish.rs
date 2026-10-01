pub mod m { pub struct B<T>(pub T); }
pub fn a<T>() {}
pub fn f() { a::<m::B::<u8>>(); }
