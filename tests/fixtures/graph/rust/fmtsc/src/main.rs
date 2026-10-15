use std::error::Error;
use std::fmt;
#[derive(Debug, Clone, Copy)]
pub struct A;
impl fmt::Display for A {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result { write!(f, "A Display") }
}
impl Error for A {}
#[derive(Debug)]
pub enum E { Wrap(A) }
impl fmt::Display for E {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self { Self::Wrap(err) => err.fmt(f) }
    }
}
fn main() { println!("{}\n{}", E::Wrap(A), std::backtrace::Backtrace::force_capture()); }
