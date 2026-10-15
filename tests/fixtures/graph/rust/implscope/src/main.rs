pub mod p {
    pub trait Show { fn show(&self) -> String; }
}
use p::Show as _;
pub mod q {
    // Show is not imported here; only the enclosing impl names it.
    pub struct A;
    impl crate::p::Show for A { fn show(&self) -> String { "A show".into() } }
    pub struct B(pub A);
    impl crate::p::Show for B {
        fn show(&self) -> String { self.0.show() }              // ref-to-field receiver
    }
    pub enum E { W(A) }
    impl crate::p::Show for E {
        fn show(&self) -> String { match self { E::W(a) => a.show() } }  // a: &A
    }
    pub struct C(pub A);
    impl crate::p::Show for C {
        fn show(&self) -> String { let a = A; a.show() }        // owned receiver
    }
}
fn main() {
    use p::Show;
    println!("{}\n{}", q::B(q::A).show(), std::backtrace::Backtrace::force_capture());
    println!("{}\n{}", q::E::W(q::A).show(), std::backtrace::Backtrace::force_capture());
    println!("{}\n{}", q::C(q::A).show(), std::backtrace::Backtrace::force_capture());
}
