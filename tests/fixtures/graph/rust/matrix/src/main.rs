pub mod c0 {
    #[derive(Clone, Copy)] pub struct S;
    pub trait Tr { fn m(self); }
    impl Tr for S { fn m(self) { println!("c0 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe() { let mut v = S; v.m(); }
    pub fn drive() { probe(); }
}
pub mod c1 {
    #[derive(Clone, Copy)] pub struct S;
    pub trait Tr { fn m(self); }
    impl Tr for &S { fn m(self) { println!("c1 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe() { let mut v = S; v.m(); }
    pub fn drive() { probe(); }
}
pub mod c2 {
    #[derive(Clone, Copy)] pub struct S;
    pub trait Tr { fn m(self); }
    impl Tr for &mut S { fn m(self) { println!("c2 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe() { let mut v = S; v.m(); }
    pub fn drive() { probe(); }
}
pub mod c3 {
    #[derive(Clone, Copy)] pub struct S;
    pub trait Tr { fn m(&self); }
    impl Tr for S { fn m(&self) { println!("c3 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe() { let mut v = S; v.m(); }
    pub fn drive() { probe(); }
}
pub mod c4 {
    #[derive(Clone, Copy)] pub struct S;
    pub trait Tr { fn m(&mut self); }
    impl Tr for S { fn m(&mut self) { println!("c4 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe() { let mut v = S; v.m(); }
    pub fn drive() { probe(); }
}
pub mod c5 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(self) { println!("c5 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(self); }
    impl Tr for S { fn m(self) { println!("c5 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe() { let mut v = S; v.m(); }
    pub fn drive() { probe(); }
}
pub mod c6 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(self) { println!("c6 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(self); }
    impl Tr for &S { fn m(self) { println!("c6 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe() { let mut v = S; v.m(); }
    pub fn drive() { probe(); }
}
pub mod c7 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(self) { println!("c7 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(self); }
    impl Tr for &mut S { fn m(self) { println!("c7 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe() { let mut v = S; v.m(); }
    pub fn drive() { probe(); }
}
pub mod c8 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(self) { println!("c8 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(self); }
    impl Tr for Box<S> { fn m(self) { println!("c8 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe() { let mut v = S; v.m(); }
    pub fn drive() { probe(); }
}
pub mod c9 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(self) { println!("c9 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&self); }
    impl Tr for S { fn m(&self) { println!("c9 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe() { let mut v = S; v.m(); }
    pub fn drive() { probe(); }
}
pub mod c10 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(self) { println!("c10 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&self); }
    impl Tr for &S { fn m(&self) { println!("c10 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe() { let mut v = S; v.m(); }
    pub fn drive() { probe(); }
}
pub mod c11 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(self) { println!("c11 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&self); }
    impl Tr for &mut S { fn m(&self) { println!("c11 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe() { let mut v = S; v.m(); }
    pub fn drive() { probe(); }
}
pub mod c12 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(self) { println!("c12 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&self); }
    impl Tr for Box<S> { fn m(&self) { println!("c12 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe() { let mut v = S; v.m(); }
    pub fn drive() { probe(); }
}
pub mod c13 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(self) { println!("c13 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&mut self); }
    impl Tr for S { fn m(&mut self) { println!("c13 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe() { let mut v = S; v.m(); }
    pub fn drive() { probe(); }
}
pub mod c14 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(self) { println!("c14 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&mut self); }
    impl Tr for &S { fn m(&mut self) { println!("c14 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe() { let mut v = S; v.m(); }
    pub fn drive() { probe(); }
}
pub mod c15 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(self) { println!("c15 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&mut self); }
    impl Tr for &mut S { fn m(&mut self) { println!("c15 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe() { let mut v = S; v.m(); }
    pub fn drive() { probe(); }
}
pub mod c16 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(self) { println!("c16 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&mut self); }
    impl Tr for Box<S> { fn m(&mut self) { println!("c16 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe() { let mut v = S; v.m(); }
    pub fn drive() { probe(); }
}
pub mod c17 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(&self) { println!("c17 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(self); }
    impl Tr for S { fn m(self) { println!("c17 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe() { let mut v = S; v.m(); }
    pub fn drive() { probe(); }
}
pub mod c18 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(&self) { println!("c18 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(self); }
    impl Tr for &S { fn m(self) { println!("c18 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe() { let mut v = S; v.m(); }
    pub fn drive() { probe(); }
}
pub mod c19 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(&self) { println!("c19 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(self); }
    impl Tr for &mut S { fn m(self) { println!("c19 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe() { let mut v = S; v.m(); }
    pub fn drive() { probe(); }
}
pub mod c20 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(&self) { println!("c20 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(self); }
    impl Tr for Box<S> { fn m(self) { println!("c20 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe() { let mut v = S; v.m(); }
    pub fn drive() { probe(); }
}
pub mod c21 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(&self) { println!("c21 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&self); }
    impl Tr for S { fn m(&self) { println!("c21 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe() { let mut v = S; v.m(); }
    pub fn drive() { probe(); }
}
pub mod c22 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(&self) { println!("c22 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&self); }
    impl Tr for &S { fn m(&self) { println!("c22 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe() { let mut v = S; v.m(); }
    pub fn drive() { probe(); }
}
pub mod c23 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(&self) { println!("c23 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&self); }
    impl Tr for &mut S { fn m(&self) { println!("c23 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe() { let mut v = S; v.m(); }
    pub fn drive() { probe(); }
}
pub mod c24 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(&self) { println!("c24 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&self); }
    impl Tr for Box<S> { fn m(&self) { println!("c24 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe() { let mut v = S; v.m(); }
    pub fn drive() { probe(); }
}
pub mod c25 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(&self) { println!("c25 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&mut self); }
    impl Tr for S { fn m(&mut self) { println!("c25 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe() { let mut v = S; v.m(); }
    pub fn drive() { probe(); }
}
pub mod c26 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(&self) { println!("c26 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&mut self); }
    impl Tr for &S { fn m(&mut self) { println!("c26 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe() { let mut v = S; v.m(); }
    pub fn drive() { probe(); }
}
pub mod c27 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(&self) { println!("c27 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&mut self); }
    impl Tr for &mut S { fn m(&mut self) { println!("c27 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe() { let mut v = S; v.m(); }
    pub fn drive() { probe(); }
}
pub mod c28 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(&self) { println!("c28 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&mut self); }
    impl Tr for Box<S> { fn m(&mut self) { println!("c28 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe() { let mut v = S; v.m(); }
    pub fn drive() { probe(); }
}
pub mod c29 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(&mut self) { println!("c29 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(self); }
    impl Tr for S { fn m(self) { println!("c29 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe() { let mut v = S; v.m(); }
    pub fn drive() { probe(); }
}
pub mod c30 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(&mut self) { println!("c30 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(self); }
    impl Tr for &S { fn m(self) { println!("c30 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe() { let mut v = S; v.m(); }
    pub fn drive() { probe(); }
}
pub mod c31 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(&mut self) { println!("c31 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(self); }
    impl Tr for &mut S { fn m(self) { println!("c31 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe() { let mut v = S; v.m(); }
    pub fn drive() { probe(); }
}
pub mod c32 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(&mut self) { println!("c32 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(self); }
    impl Tr for Box<S> { fn m(self) { println!("c32 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe() { let mut v = S; v.m(); }
    pub fn drive() { probe(); }
}
pub mod c33 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(&mut self) { println!("c33 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&self); }
    impl Tr for S { fn m(&self) { println!("c33 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe() { let mut v = S; v.m(); }
    pub fn drive() { probe(); }
}
pub mod c34 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(&mut self) { println!("c34 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&self); }
    impl Tr for &S { fn m(&self) { println!("c34 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe() { let mut v = S; v.m(); }
    pub fn drive() { probe(); }
}
pub mod c35 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(&mut self) { println!("c35 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&self); }
    impl Tr for &mut S { fn m(&self) { println!("c35 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe() { let mut v = S; v.m(); }
    pub fn drive() { probe(); }
}
pub mod c36 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(&mut self) { println!("c36 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&self); }
    impl Tr for Box<S> { fn m(&self) { println!("c36 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe() { let mut v = S; v.m(); }
    pub fn drive() { probe(); }
}
pub mod c37 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(&mut self) { println!("c37 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&mut self); }
    impl Tr for S { fn m(&mut self) { println!("c37 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe() { let mut v = S; v.m(); }
    pub fn drive() { probe(); }
}
pub mod c38 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(&mut self) { println!("c38 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&mut self); }
    impl Tr for &S { fn m(&mut self) { println!("c38 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe() { let mut v = S; v.m(); }
    pub fn drive() { probe(); }
}
pub mod c39 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(&mut self) { println!("c39 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&mut self); }
    impl Tr for &mut S { fn m(&mut self) { println!("c39 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe() { let mut v = S; v.m(); }
    pub fn drive() { probe(); }
}
pub mod c40 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(&mut self) { println!("c40 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&mut self); }
    impl Tr for Box<S> { fn m(&mut self) { println!("c40 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe() { let mut v = S; v.m(); }
    pub fn drive() { probe(); }
}
pub mod c41 {
    #[derive(Clone, Copy)] pub struct S;
    pub trait Tr { fn m(self); }
    impl Tr for S { fn m(self) { println!("c41 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &S) { v.m(); }
    pub fn drive() { probe(&S); }
}
pub mod c42 {
    #[derive(Clone, Copy)] pub struct S;
    pub trait Tr { fn m(self); }
    impl Tr for &S { fn m(self) { println!("c42 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &S) { v.m(); }
    pub fn drive() { probe(&S); }
}
pub mod c43 {
    #[derive(Clone, Copy)] pub struct S;
    pub trait Tr { fn m(&self); }
    impl Tr for S { fn m(&self) { println!("c43 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &S) { v.m(); }
    pub fn drive() { probe(&S); }
}
pub mod c44 {
    #[derive(Clone, Copy)] pub struct S;
    pub trait Tr { fn m(&self); }
    impl Tr for &S { fn m(&self) { println!("c44 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &S) { v.m(); }
    pub fn drive() { probe(&S); }
}
pub mod c45 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(self) { println!("c45 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(self); }
    impl Tr for S { fn m(self) { println!("c45 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &S) { v.m(); }
    pub fn drive() { probe(&S); }
}
pub mod c46 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(self) { println!("c46 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(self); }
    impl Tr for &S { fn m(self) { println!("c46 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &S) { v.m(); }
    pub fn drive() { probe(&S); }
}
pub mod c47 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(self) { println!("c47 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(self); }
    impl Tr for &mut S { fn m(self) { println!("c47 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &S) { v.m(); }
    pub fn drive() { probe(&S); }
}
pub mod c48 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(self) { println!("c48 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(self); }
    impl Tr for Box<S> { fn m(self) { println!("c48 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &S) { v.m(); }
    pub fn drive() { probe(&S); }
}
pub mod c49 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(self) { println!("c49 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&self); }
    impl Tr for S { fn m(&self) { println!("c49 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &S) { v.m(); }
    pub fn drive() { probe(&S); }
}
pub mod c50 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(self) { println!("c50 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&self); }
    impl Tr for &S { fn m(&self) { println!("c50 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &S) { v.m(); }
    pub fn drive() { probe(&S); }
}
pub mod c51 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(self) { println!("c51 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&self); }
    impl Tr for &mut S { fn m(&self) { println!("c51 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &S) { v.m(); }
    pub fn drive() { probe(&S); }
}
pub mod c52 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(self) { println!("c52 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&self); }
    impl Tr for Box<S> { fn m(&self) { println!("c52 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &S) { v.m(); }
    pub fn drive() { probe(&S); }
}
pub mod c53 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(self) { println!("c53 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&mut self); }
    impl Tr for S { fn m(&mut self) { println!("c53 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &S) { v.m(); }
    pub fn drive() { probe(&S); }
}
pub mod c54 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(self) { println!("c54 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&mut self); }
    impl Tr for &mut S { fn m(&mut self) { println!("c54 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &S) { v.m(); }
    pub fn drive() { probe(&S); }
}
pub mod c55 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(self) { println!("c55 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&mut self); }
    impl Tr for Box<S> { fn m(&mut self) { println!("c55 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &S) { v.m(); }
    pub fn drive() { probe(&S); }
}
pub mod c56 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(&self) { println!("c56 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(self); }
    impl Tr for S { fn m(self) { println!("c56 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &S) { v.m(); }
    pub fn drive() { probe(&S); }
}
pub mod c57 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(&self) { println!("c57 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(self); }
    impl Tr for &S { fn m(self) { println!("c57 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &S) { v.m(); }
    pub fn drive() { probe(&S); }
}
pub mod c58 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(&self) { println!("c58 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(self); }
    impl Tr for &mut S { fn m(self) { println!("c58 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &S) { v.m(); }
    pub fn drive() { probe(&S); }
}
pub mod c59 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(&self) { println!("c59 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(self); }
    impl Tr for Box<S> { fn m(self) { println!("c59 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &S) { v.m(); }
    pub fn drive() { probe(&S); }
}
pub mod c60 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(&self) { println!("c60 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&self); }
    impl Tr for S { fn m(&self) { println!("c60 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &S) { v.m(); }
    pub fn drive() { probe(&S); }
}
pub mod c61 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(&self) { println!("c61 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&self); }
    impl Tr for &S { fn m(&self) { println!("c61 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &S) { v.m(); }
    pub fn drive() { probe(&S); }
}
pub mod c62 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(&self) { println!("c62 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&self); }
    impl Tr for &mut S { fn m(&self) { println!("c62 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &S) { v.m(); }
    pub fn drive() { probe(&S); }
}
pub mod c63 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(&self) { println!("c63 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&self); }
    impl Tr for Box<S> { fn m(&self) { println!("c63 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &S) { v.m(); }
    pub fn drive() { probe(&S); }
}
pub mod c64 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(&self) { println!("c64 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&mut self); }
    impl Tr for S { fn m(&mut self) { println!("c64 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &S) { v.m(); }
    pub fn drive() { probe(&S); }
}
pub mod c65 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(&self) { println!("c65 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&mut self); }
    impl Tr for &S { fn m(&mut self) { println!("c65 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &S) { v.m(); }
    pub fn drive() { probe(&S); }
}
pub mod c66 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(&self) { println!("c66 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&mut self); }
    impl Tr for &mut S { fn m(&mut self) { println!("c66 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &S) { v.m(); }
    pub fn drive() { probe(&S); }
}
pub mod c67 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(&self) { println!("c67 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&mut self); }
    impl Tr for Box<S> { fn m(&mut self) { println!("c67 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &S) { v.m(); }
    pub fn drive() { probe(&S); }
}
pub mod c68 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(&mut self) { println!("c68 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(self); }
    impl Tr for S { fn m(self) { println!("c68 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &S) { v.m(); }
    pub fn drive() { probe(&S); }
}
pub mod c69 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(&mut self) { println!("c69 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(self); }
    impl Tr for &S { fn m(self) { println!("c69 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &S) { v.m(); }
    pub fn drive() { probe(&S); }
}
pub mod c70 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(&mut self) { println!("c70 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&self); }
    impl Tr for S { fn m(&self) { println!("c70 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &S) { v.m(); }
    pub fn drive() { probe(&S); }
}
pub mod c71 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(&mut self) { println!("c71 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&self); }
    impl Tr for &S { fn m(&self) { println!("c71 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &S) { v.m(); }
    pub fn drive() { probe(&S); }
}
pub mod c72 {
    #[derive(Clone, Copy)] pub struct S;
    pub trait Tr { fn m(self); }
    impl Tr for S { fn m(self) { println!("c72 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut S) { v.m(); }
    pub fn drive() { probe(&mut S); }
}
pub mod c73 {
    #[derive(Clone, Copy)] pub struct S;
    pub trait Tr { fn m(self); }
    impl Tr for &S { fn m(self) { println!("c73 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut S) { v.m(); }
    pub fn drive() { probe(&mut S); }
}
pub mod c74 {
    #[derive(Clone, Copy)] pub struct S;
    pub trait Tr { fn m(self); }
    impl Tr for &mut S { fn m(self) { println!("c74 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut S) { v.m(); }
    pub fn drive() { probe(&mut S); }
}
pub mod c75 {
    #[derive(Clone, Copy)] pub struct S;
    pub trait Tr { fn m(&self); }
    impl Tr for S { fn m(&self) { println!("c75 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut S) { v.m(); }
    pub fn drive() { probe(&mut S); }
}
pub mod c76 {
    #[derive(Clone, Copy)] pub struct S;
    pub trait Tr { fn m(&self); }
    impl Tr for &mut S { fn m(&self) { println!("c76 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut S) { v.m(); }
    pub fn drive() { probe(&mut S); }
}
pub mod c77 {
    #[derive(Clone, Copy)] pub struct S;
    pub trait Tr { fn m(&mut self); }
    impl Tr for S { fn m(&mut self) { println!("c77 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut S) { v.m(); }
    pub fn drive() { probe(&mut S); }
}
pub mod c78 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(self) { println!("c78 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(self); }
    impl Tr for S { fn m(self) { println!("c78 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut S) { v.m(); }
    pub fn drive() { probe(&mut S); }
}
pub mod c79 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(self) { println!("c79 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(self); }
    impl Tr for &S { fn m(self) { println!("c79 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut S) { v.m(); }
    pub fn drive() { probe(&mut S); }
}
pub mod c80 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(self) { println!("c80 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(self); }
    impl Tr for &mut S { fn m(self) { println!("c80 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut S) { v.m(); }
    pub fn drive() { probe(&mut S); }
}
pub mod c81 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(self) { println!("c81 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(self); }
    impl Tr for Box<S> { fn m(self) { println!("c81 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut S) { v.m(); }
    pub fn drive() { probe(&mut S); }
}
pub mod c82 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(self) { println!("c82 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&self); }
    impl Tr for S { fn m(&self) { println!("c82 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut S) { v.m(); }
    pub fn drive() { probe(&mut S); }
}
pub mod c83 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(self) { println!("c83 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&self); }
    impl Tr for &S { fn m(&self) { println!("c83 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut S) { v.m(); }
    pub fn drive() { probe(&mut S); }
}
pub mod c84 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(self) { println!("c84 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&self); }
    impl Tr for &mut S { fn m(&self) { println!("c84 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut S) { v.m(); }
    pub fn drive() { probe(&mut S); }
}
pub mod c85 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(self) { println!("c85 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&self); }
    impl Tr for Box<S> { fn m(&self) { println!("c85 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut S) { v.m(); }
    pub fn drive() { probe(&mut S); }
}
pub mod c86 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(self) { println!("c86 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&mut self); }
    impl Tr for S { fn m(&mut self) { println!("c86 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut S) { v.m(); }
    pub fn drive() { probe(&mut S); }
}
pub mod c87 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(self) { println!("c87 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&mut self); }
    impl Tr for &S { fn m(&mut self) { println!("c87 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut S) { v.m(); }
    pub fn drive() { probe(&mut S); }
}
pub mod c88 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(self) { println!("c88 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&mut self); }
    impl Tr for Box<S> { fn m(&mut self) { println!("c88 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut S) { v.m(); }
    pub fn drive() { probe(&mut S); }
}
pub mod c89 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(&self) { println!("c89 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(self); }
    impl Tr for S { fn m(self) { println!("c89 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut S) { v.m(); }
    pub fn drive() { probe(&mut S); }
}
pub mod c90 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(&self) { println!("c90 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(self); }
    impl Tr for &S { fn m(self) { println!("c90 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut S) { v.m(); }
    pub fn drive() { probe(&mut S); }
}
pub mod c91 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(&self) { println!("c91 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(self); }
    impl Tr for &mut S { fn m(self) { println!("c91 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut S) { v.m(); }
    pub fn drive() { probe(&mut S); }
}
pub mod c92 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(&self) { println!("c92 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(self); }
    impl Tr for Box<S> { fn m(self) { println!("c92 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut S) { v.m(); }
    pub fn drive() { probe(&mut S); }
}
pub mod c93 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(&self) { println!("c93 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&self); }
    impl Tr for S { fn m(&self) { println!("c93 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut S) { v.m(); }
    pub fn drive() { probe(&mut S); }
}
pub mod c94 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(&self) { println!("c94 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&self); }
    impl Tr for &S { fn m(&self) { println!("c94 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut S) { v.m(); }
    pub fn drive() { probe(&mut S); }
}
pub mod c95 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(&self) { println!("c95 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&self); }
    impl Tr for &mut S { fn m(&self) { println!("c95 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut S) { v.m(); }
    pub fn drive() { probe(&mut S); }
}
pub mod c96 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(&self) { println!("c96 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&self); }
    impl Tr for Box<S> { fn m(&self) { println!("c96 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut S) { v.m(); }
    pub fn drive() { probe(&mut S); }
}
pub mod c97 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(&self) { println!("c97 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&mut self); }
    impl Tr for S { fn m(&mut self) { println!("c97 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut S) { v.m(); }
    pub fn drive() { probe(&mut S); }
}
pub mod c98 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(&self) { println!("c98 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&mut self); }
    impl Tr for &S { fn m(&mut self) { println!("c98 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut S) { v.m(); }
    pub fn drive() { probe(&mut S); }
}
pub mod c99 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(&self) { println!("c99 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&mut self); }
    impl Tr for Box<S> { fn m(&mut self) { println!("c99 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut S) { v.m(); }
    pub fn drive() { probe(&mut S); }
}
pub mod c100 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(&mut self) { println!("c100 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(self); }
    impl Tr for S { fn m(self) { println!("c100 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut S) { v.m(); }
    pub fn drive() { probe(&mut S); }
}
pub mod c101 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(&mut self) { println!("c101 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(self); }
    impl Tr for &S { fn m(self) { println!("c101 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut S) { v.m(); }
    pub fn drive() { probe(&mut S); }
}
pub mod c102 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(&mut self) { println!("c102 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(self); }
    impl Tr for &mut S { fn m(self) { println!("c102 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut S) { v.m(); }
    pub fn drive() { probe(&mut S); }
}
pub mod c103 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(&mut self) { println!("c103 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(self); }
    impl Tr for Box<S> { fn m(self) { println!("c103 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut S) { v.m(); }
    pub fn drive() { probe(&mut S); }
}
pub mod c104 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(&mut self) { println!("c104 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&self); }
    impl Tr for S { fn m(&self) { println!("c104 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut S) { v.m(); }
    pub fn drive() { probe(&mut S); }
}
pub mod c105 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(&mut self) { println!("c105 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&self); }
    impl Tr for &S { fn m(&self) { println!("c105 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut S) { v.m(); }
    pub fn drive() { probe(&mut S); }
}
pub mod c106 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(&mut self) { println!("c106 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&self); }
    impl Tr for &mut S { fn m(&self) { println!("c106 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut S) { v.m(); }
    pub fn drive() { probe(&mut S); }
}
pub mod c107 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(&mut self) { println!("c107 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&self); }
    impl Tr for Box<S> { fn m(&self) { println!("c107 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut S) { v.m(); }
    pub fn drive() { probe(&mut S); }
}
pub mod c108 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(&mut self) { println!("c108 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&mut self); }
    impl Tr for S { fn m(&mut self) { println!("c108 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut S) { v.m(); }
    pub fn drive() { probe(&mut S); }
}
pub mod c109 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(&mut self) { println!("c109 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&mut self); }
    impl Tr for &S { fn m(&mut self) { println!("c109 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut S) { v.m(); }
    pub fn drive() { probe(&mut S); }
}
pub mod c110 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(&mut self) { println!("c110 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&mut self); }
    impl Tr for &mut S { fn m(&mut self) { println!("c110 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut S) { v.m(); }
    pub fn drive() { probe(&mut S); }
}
pub mod c111 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(&mut self) { println!("c111 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&mut self); }
    impl Tr for Box<S> { fn m(&mut self) { println!("c111 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut S) { v.m(); }
    pub fn drive() { probe(&mut S); }
}
pub mod c112 {
    #[derive(Clone, Copy)] pub struct S;
    pub trait Tr { fn m(self); }
    impl Tr for S { fn m(self) { println!("c112 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(mut v: Box<S>) { v.m(); }
    pub fn drive() { probe(Box::new(S)); }
}
pub mod c113 {
    #[derive(Clone, Copy)] pub struct S;
    pub trait Tr { fn m(self); }
    impl Tr for &S { fn m(self) { println!("c113 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(mut v: Box<S>) { v.m(); }
    pub fn drive() { probe(Box::new(S)); }
}
pub mod c114 {
    #[derive(Clone, Copy)] pub struct S;
    pub trait Tr { fn m(self); }
    impl Tr for &mut S { fn m(self) { println!("c114 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(mut v: Box<S>) { v.m(); }
    pub fn drive() { probe(Box::new(S)); }
}
pub mod c115 {
    #[derive(Clone, Copy)] pub struct S;
    pub trait Tr { fn m(self); }
    impl Tr for Box<S> { fn m(self) { println!("c115 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(mut v: Box<S>) { v.m(); }
    pub fn drive() { probe(Box::new(S)); }
}
pub mod c116 {
    #[derive(Clone, Copy)] pub struct S;
    pub trait Tr { fn m(&self); }
    impl Tr for S { fn m(&self) { println!("c116 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(mut v: Box<S>) { v.m(); }
    pub fn drive() { probe(Box::new(S)); }
}
pub mod c117 {
    #[derive(Clone, Copy)] pub struct S;
    pub trait Tr { fn m(&self); }
    impl Tr for Box<S> { fn m(&self) { println!("c117 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(mut v: Box<S>) { v.m(); }
    pub fn drive() { probe(Box::new(S)); }
}
pub mod c118 {
    #[derive(Clone, Copy)] pub struct S;
    pub trait Tr { fn m(&mut self); }
    impl Tr for S { fn m(&mut self) { println!("c118 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(mut v: Box<S>) { v.m(); }
    pub fn drive() { probe(Box::new(S)); }
}
pub mod c119 {
    #[derive(Clone, Copy)] pub struct S;
    pub trait Tr { fn m(&mut self); }
    impl Tr for Box<S> { fn m(&mut self) { println!("c119 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(mut v: Box<S>) { v.m(); }
    pub fn drive() { probe(Box::new(S)); }
}
pub mod c120 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(self) { println!("c120 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(self); }
    impl Tr for S { fn m(self) { println!("c120 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(mut v: Box<S>) { v.m(); }
    pub fn drive() { probe(Box::new(S)); }
}
pub mod c121 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(self) { println!("c121 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(self); }
    impl Tr for &S { fn m(self) { println!("c121 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(mut v: Box<S>) { v.m(); }
    pub fn drive() { probe(Box::new(S)); }
}
pub mod c122 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(self) { println!("c122 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(self); }
    impl Tr for &mut S { fn m(self) { println!("c122 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(mut v: Box<S>) { v.m(); }
    pub fn drive() { probe(Box::new(S)); }
}
pub mod c123 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(self) { println!("c123 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(self); }
    impl Tr for Box<S> { fn m(self) { println!("c123 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(mut v: Box<S>) { v.m(); }
    pub fn drive() { probe(Box::new(S)); }
}
pub mod c124 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(self) { println!("c124 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&self); }
    impl Tr for S { fn m(&self) { println!("c124 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(mut v: Box<S>) { v.m(); }
    pub fn drive() { probe(Box::new(S)); }
}
pub mod c125 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(self) { println!("c125 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&self); }
    impl Tr for &S { fn m(&self) { println!("c125 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(mut v: Box<S>) { v.m(); }
    pub fn drive() { probe(Box::new(S)); }
}
pub mod c126 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(self) { println!("c126 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&self); }
    impl Tr for &mut S { fn m(&self) { println!("c126 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(mut v: Box<S>) { v.m(); }
    pub fn drive() { probe(Box::new(S)); }
}
pub mod c127 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(self) { println!("c127 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&self); }
    impl Tr for Box<S> { fn m(&self) { println!("c127 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(mut v: Box<S>) { v.m(); }
    pub fn drive() { probe(Box::new(S)); }
}
pub mod c128 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(self) { println!("c128 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&mut self); }
    impl Tr for S { fn m(&mut self) { println!("c128 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(mut v: Box<S>) { v.m(); }
    pub fn drive() { probe(Box::new(S)); }
}
pub mod c129 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(self) { println!("c129 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&mut self); }
    impl Tr for &S { fn m(&mut self) { println!("c129 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(mut v: Box<S>) { v.m(); }
    pub fn drive() { probe(Box::new(S)); }
}
pub mod c130 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(self) { println!("c130 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&mut self); }
    impl Tr for &mut S { fn m(&mut self) { println!("c130 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(mut v: Box<S>) { v.m(); }
    pub fn drive() { probe(Box::new(S)); }
}
pub mod c131 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(self) { println!("c131 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&mut self); }
    impl Tr for Box<S> { fn m(&mut self) { println!("c131 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(mut v: Box<S>) { v.m(); }
    pub fn drive() { probe(Box::new(S)); }
}
pub mod c132 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(&self) { println!("c132 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(self); }
    impl Tr for S { fn m(self) { println!("c132 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(mut v: Box<S>) { v.m(); }
    pub fn drive() { probe(Box::new(S)); }
}
pub mod c133 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(&self) { println!("c133 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(self); }
    impl Tr for &S { fn m(self) { println!("c133 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(mut v: Box<S>) { v.m(); }
    pub fn drive() { probe(Box::new(S)); }
}
pub mod c134 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(&self) { println!("c134 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(self); }
    impl Tr for &mut S { fn m(self) { println!("c134 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(mut v: Box<S>) { v.m(); }
    pub fn drive() { probe(Box::new(S)); }
}
pub mod c135 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(&self) { println!("c135 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(self); }
    impl Tr for Box<S> { fn m(self) { println!("c135 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(mut v: Box<S>) { v.m(); }
    pub fn drive() { probe(Box::new(S)); }
}
pub mod c136 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(&self) { println!("c136 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&self); }
    impl Tr for S { fn m(&self) { println!("c136 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(mut v: Box<S>) { v.m(); }
    pub fn drive() { probe(Box::new(S)); }
}
pub mod c137 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(&self) { println!("c137 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&self); }
    impl Tr for &S { fn m(&self) { println!("c137 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(mut v: Box<S>) { v.m(); }
    pub fn drive() { probe(Box::new(S)); }
}
pub mod c138 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(&self) { println!("c138 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&self); }
    impl Tr for &mut S { fn m(&self) { println!("c138 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(mut v: Box<S>) { v.m(); }
    pub fn drive() { probe(Box::new(S)); }
}
pub mod c139 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(&self) { println!("c139 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&self); }
    impl Tr for Box<S> { fn m(&self) { println!("c139 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(mut v: Box<S>) { v.m(); }
    pub fn drive() { probe(Box::new(S)); }
}
pub mod c140 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(&self) { println!("c140 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&mut self); }
    impl Tr for S { fn m(&mut self) { println!("c140 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(mut v: Box<S>) { v.m(); }
    pub fn drive() { probe(Box::new(S)); }
}
pub mod c141 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(&self) { println!("c141 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&mut self); }
    impl Tr for &S { fn m(&mut self) { println!("c141 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(mut v: Box<S>) { v.m(); }
    pub fn drive() { probe(Box::new(S)); }
}
pub mod c142 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(&self) { println!("c142 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&mut self); }
    impl Tr for &mut S { fn m(&mut self) { println!("c142 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(mut v: Box<S>) { v.m(); }
    pub fn drive() { probe(Box::new(S)); }
}
pub mod c143 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(&self) { println!("c143 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&mut self); }
    impl Tr for Box<S> { fn m(&mut self) { println!("c143 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(mut v: Box<S>) { v.m(); }
    pub fn drive() { probe(Box::new(S)); }
}
pub mod c144 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(&mut self) { println!("c144 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(self); }
    impl Tr for S { fn m(self) { println!("c144 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(mut v: Box<S>) { v.m(); }
    pub fn drive() { probe(Box::new(S)); }
}
pub mod c145 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(&mut self) { println!("c145 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(self); }
    impl Tr for &S { fn m(self) { println!("c145 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(mut v: Box<S>) { v.m(); }
    pub fn drive() { probe(Box::new(S)); }
}
pub mod c146 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(&mut self) { println!("c146 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(self); }
    impl Tr for &mut S { fn m(self) { println!("c146 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(mut v: Box<S>) { v.m(); }
    pub fn drive() { probe(Box::new(S)); }
}
pub mod c147 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(&mut self) { println!("c147 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(self); }
    impl Tr for Box<S> { fn m(self) { println!("c147 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(mut v: Box<S>) { v.m(); }
    pub fn drive() { probe(Box::new(S)); }
}
pub mod c148 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(&mut self) { println!("c148 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&self); }
    impl Tr for S { fn m(&self) { println!("c148 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(mut v: Box<S>) { v.m(); }
    pub fn drive() { probe(Box::new(S)); }
}
pub mod c149 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(&mut self) { println!("c149 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&self); }
    impl Tr for &S { fn m(&self) { println!("c149 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(mut v: Box<S>) { v.m(); }
    pub fn drive() { probe(Box::new(S)); }
}
pub mod c150 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(&mut self) { println!("c150 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&self); }
    impl Tr for &mut S { fn m(&self) { println!("c150 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(mut v: Box<S>) { v.m(); }
    pub fn drive() { probe(Box::new(S)); }
}
pub mod c151 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(&mut self) { println!("c151 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&self); }
    impl Tr for Box<S> { fn m(&self) { println!("c151 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(mut v: Box<S>) { v.m(); }
    pub fn drive() { probe(Box::new(S)); }
}
pub mod c152 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(&mut self) { println!("c152 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&mut self); }
    impl Tr for S { fn m(&mut self) { println!("c152 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(mut v: Box<S>) { v.m(); }
    pub fn drive() { probe(Box::new(S)); }
}
pub mod c153 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(&mut self) { println!("c153 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&mut self); }
    impl Tr for &S { fn m(&mut self) { println!("c153 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(mut v: Box<S>) { v.m(); }
    pub fn drive() { probe(Box::new(S)); }
}
pub mod c154 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(&mut self) { println!("c154 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&mut self); }
    impl Tr for &mut S { fn m(&mut self) { println!("c154 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(mut v: Box<S>) { v.m(); }
    pub fn drive() { probe(Box::new(S)); }
}
pub mod c155 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(&mut self) { println!("c155 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&mut self); }
    impl Tr for Box<S> { fn m(&mut self) { println!("c155 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(mut v: Box<S>) { v.m(); }
    pub fn drive() { probe(Box::new(S)); }
}
pub mod c156 {
    #[derive(Clone, Copy)] pub struct S;
    pub trait Tr { fn m(self); }
    impl Tr for S { fn m(self) { println!("c156 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &&S) { v.m(); }
    pub fn drive() { probe(&&S); }
}
pub mod c157 {
    #[derive(Clone, Copy)] pub struct S;
    pub trait Tr { fn m(self); }
    impl Tr for &S { fn m(self) { println!("c157 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &&S) { v.m(); }
    pub fn drive() { probe(&&S); }
}
pub mod c158 {
    #[derive(Clone, Copy)] pub struct S;
    pub trait Tr { fn m(&self); }
    impl Tr for S { fn m(&self) { println!("c158 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &&S) { v.m(); }
    pub fn drive() { probe(&&S); }
}
pub mod c159 {
    #[derive(Clone, Copy)] pub struct S;
    pub trait Tr { fn m(&self); }
    impl Tr for &S { fn m(&self) { println!("c159 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &&S) { v.m(); }
    pub fn drive() { probe(&&S); }
}
pub mod c160 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(self) { println!("c160 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(self); }
    impl Tr for S { fn m(self) { println!("c160 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &&S) { v.m(); }
    pub fn drive() { probe(&&S); }
}
pub mod c161 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(self) { println!("c161 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(self); }
    impl Tr for &S { fn m(self) { println!("c161 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &&S) { v.m(); }
    pub fn drive() { probe(&&S); }
}
pub mod c162 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(self) { println!("c162 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(self); }
    impl Tr for &mut S { fn m(self) { println!("c162 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &&S) { v.m(); }
    pub fn drive() { probe(&&S); }
}
pub mod c163 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(self) { println!("c163 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(self); }
    impl Tr for Box<S> { fn m(self) { println!("c163 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &&S) { v.m(); }
    pub fn drive() { probe(&&S); }
}
pub mod c164 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(self) { println!("c164 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&self); }
    impl Tr for S { fn m(&self) { println!("c164 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &&S) { v.m(); }
    pub fn drive() { probe(&&S); }
}
pub mod c165 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(self) { println!("c165 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&self); }
    impl Tr for &S { fn m(&self) { println!("c165 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &&S) { v.m(); }
    pub fn drive() { probe(&&S); }
}
pub mod c166 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(self) { println!("c166 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&self); }
    impl Tr for &mut S { fn m(&self) { println!("c166 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &&S) { v.m(); }
    pub fn drive() { probe(&&S); }
}
pub mod c167 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(self) { println!("c167 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&self); }
    impl Tr for Box<S> { fn m(&self) { println!("c167 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &&S) { v.m(); }
    pub fn drive() { probe(&&S); }
}
pub mod c168 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(self) { println!("c168 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&mut self); }
    impl Tr for S { fn m(&mut self) { println!("c168 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &&S) { v.m(); }
    pub fn drive() { probe(&&S); }
}
pub mod c169 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(self) { println!("c169 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&mut self); }
    impl Tr for &mut S { fn m(&mut self) { println!("c169 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &&S) { v.m(); }
    pub fn drive() { probe(&&S); }
}
pub mod c170 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(self) { println!("c170 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&mut self); }
    impl Tr for Box<S> { fn m(&mut self) { println!("c170 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &&S) { v.m(); }
    pub fn drive() { probe(&&S); }
}
pub mod c171 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(&self) { println!("c171 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(self); }
    impl Tr for S { fn m(self) { println!("c171 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &&S) { v.m(); }
    pub fn drive() { probe(&&S); }
}
pub mod c172 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(&self) { println!("c172 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(self); }
    impl Tr for &S { fn m(self) { println!("c172 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &&S) { v.m(); }
    pub fn drive() { probe(&&S); }
}
pub mod c173 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(&self) { println!("c173 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(self); }
    impl Tr for &mut S { fn m(self) { println!("c173 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &&S) { v.m(); }
    pub fn drive() { probe(&&S); }
}
pub mod c174 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(&self) { println!("c174 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(self); }
    impl Tr for Box<S> { fn m(self) { println!("c174 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &&S) { v.m(); }
    pub fn drive() { probe(&&S); }
}
pub mod c175 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(&self) { println!("c175 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&self); }
    impl Tr for S { fn m(&self) { println!("c175 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &&S) { v.m(); }
    pub fn drive() { probe(&&S); }
}
pub mod c176 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(&self) { println!("c176 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&self); }
    impl Tr for &S { fn m(&self) { println!("c176 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &&S) { v.m(); }
    pub fn drive() { probe(&&S); }
}
pub mod c177 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(&self) { println!("c177 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&self); }
    impl Tr for &mut S { fn m(&self) { println!("c177 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &&S) { v.m(); }
    pub fn drive() { probe(&&S); }
}
pub mod c178 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(&self) { println!("c178 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&self); }
    impl Tr for Box<S> { fn m(&self) { println!("c178 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &&S) { v.m(); }
    pub fn drive() { probe(&&S); }
}
pub mod c179 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(&self) { println!("c179 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&mut self); }
    impl Tr for S { fn m(&mut self) { println!("c179 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &&S) { v.m(); }
    pub fn drive() { probe(&&S); }
}
pub mod c180 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(&self) { println!("c180 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&mut self); }
    impl Tr for &S { fn m(&mut self) { println!("c180 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &&S) { v.m(); }
    pub fn drive() { probe(&&S); }
}
pub mod c181 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(&self) { println!("c181 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&mut self); }
    impl Tr for &mut S { fn m(&mut self) { println!("c181 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &&S) { v.m(); }
    pub fn drive() { probe(&&S); }
}
pub mod c182 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(&self) { println!("c182 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&mut self); }
    impl Tr for Box<S> { fn m(&mut self) { println!("c182 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &&S) { v.m(); }
    pub fn drive() { probe(&&S); }
}
pub mod c183 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(&mut self) { println!("c183 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(self); }
    impl Tr for S { fn m(self) { println!("c183 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &&S) { v.m(); }
    pub fn drive() { probe(&&S); }
}
pub mod c184 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(&mut self) { println!("c184 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(self); }
    impl Tr for &S { fn m(self) { println!("c184 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &&S) { v.m(); }
    pub fn drive() { probe(&&S); }
}
pub mod c185 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(&mut self) { println!("c185 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&self); }
    impl Tr for S { fn m(&self) { println!("c185 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &&S) { v.m(); }
    pub fn drive() { probe(&&S); }
}
pub mod c186 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(&mut self) { println!("c186 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&self); }
    impl Tr for &S { fn m(&self) { println!("c186 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &&S) { v.m(); }
    pub fn drive() { probe(&&S); }
}
pub mod c187 {
    #[derive(Clone, Copy)] pub struct S;
    pub trait Tr { fn m(self); }
    impl Tr for S { fn m(self) { println!("c187 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &Box<S>) { v.m(); }
    pub fn drive() { probe(&Box::new(S)); }
}
pub mod c188 {
    #[derive(Clone, Copy)] pub struct S;
    pub trait Tr { fn m(self); }
    impl Tr for &S { fn m(self) { println!("c188 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &Box<S>) { v.m(); }
    pub fn drive() { probe(&Box::new(S)); }
}
pub mod c189 {
    #[derive(Clone, Copy)] pub struct S;
    pub trait Tr { fn m(&self); }
    impl Tr for S { fn m(&self) { println!("c189 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &Box<S>) { v.m(); }
    pub fn drive() { probe(&Box::new(S)); }
}
pub mod c190 {
    #[derive(Clone, Copy)] pub struct S;
    pub trait Tr { fn m(&self); }
    impl Tr for Box<S> { fn m(&self) { println!("c190 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &Box<S>) { v.m(); }
    pub fn drive() { probe(&Box::new(S)); }
}
pub mod c191 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(self) { println!("c191 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(self); }
    impl Tr for S { fn m(self) { println!("c191 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &Box<S>) { v.m(); }
    pub fn drive() { probe(&Box::new(S)); }
}
pub mod c192 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(self) { println!("c192 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(self); }
    impl Tr for &S { fn m(self) { println!("c192 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &Box<S>) { v.m(); }
    pub fn drive() { probe(&Box::new(S)); }
}
pub mod c193 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(self) { println!("c193 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(self); }
    impl Tr for &mut S { fn m(self) { println!("c193 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &Box<S>) { v.m(); }
    pub fn drive() { probe(&Box::new(S)); }
}
pub mod c194 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(self) { println!("c194 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&self); }
    impl Tr for S { fn m(&self) { println!("c194 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &Box<S>) { v.m(); }
    pub fn drive() { probe(&Box::new(S)); }
}
pub mod c195 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(self) { println!("c195 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&self); }
    impl Tr for &S { fn m(&self) { println!("c195 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &Box<S>) { v.m(); }
    pub fn drive() { probe(&Box::new(S)); }
}
pub mod c196 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(self) { println!("c196 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&self); }
    impl Tr for &mut S { fn m(&self) { println!("c196 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &Box<S>) { v.m(); }
    pub fn drive() { probe(&Box::new(S)); }
}
pub mod c197 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(self) { println!("c197 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&self); }
    impl Tr for Box<S> { fn m(&self) { println!("c197 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &Box<S>) { v.m(); }
    pub fn drive() { probe(&Box::new(S)); }
}
pub mod c198 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(self) { println!("c198 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&mut self); }
    impl Tr for S { fn m(&mut self) { println!("c198 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &Box<S>) { v.m(); }
    pub fn drive() { probe(&Box::new(S)); }
}
pub mod c199 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(self) { println!("c199 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&mut self); }
    impl Tr for &S { fn m(&mut self) { println!("c199 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &Box<S>) { v.m(); }
    pub fn drive() { probe(&Box::new(S)); }
}
pub mod c200 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(self) { println!("c200 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&mut self); }
    impl Tr for &mut S { fn m(&mut self) { println!("c200 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &Box<S>) { v.m(); }
    pub fn drive() { probe(&Box::new(S)); }
}
pub mod c201 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(&self) { println!("c201 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(self); }
    impl Tr for S { fn m(self) { println!("c201 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &Box<S>) { v.m(); }
    pub fn drive() { probe(&Box::new(S)); }
}
pub mod c202 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(&self) { println!("c202 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(self); }
    impl Tr for &S { fn m(self) { println!("c202 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &Box<S>) { v.m(); }
    pub fn drive() { probe(&Box::new(S)); }
}
pub mod c203 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(&self) { println!("c203 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(self); }
    impl Tr for &mut S { fn m(self) { println!("c203 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &Box<S>) { v.m(); }
    pub fn drive() { probe(&Box::new(S)); }
}
pub mod c204 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(&self) { println!("c204 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&self); }
    impl Tr for S { fn m(&self) { println!("c204 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &Box<S>) { v.m(); }
    pub fn drive() { probe(&Box::new(S)); }
}
pub mod c205 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(&self) { println!("c205 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&self); }
    impl Tr for &S { fn m(&self) { println!("c205 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &Box<S>) { v.m(); }
    pub fn drive() { probe(&Box::new(S)); }
}
pub mod c206 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(&self) { println!("c206 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&self); }
    impl Tr for &mut S { fn m(&self) { println!("c206 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &Box<S>) { v.m(); }
    pub fn drive() { probe(&Box::new(S)); }
}
pub mod c207 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(&self) { println!("c207 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&self); }
    impl Tr for Box<S> { fn m(&self) { println!("c207 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &Box<S>) { v.m(); }
    pub fn drive() { probe(&Box::new(S)); }
}
pub mod c208 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(&self) { println!("c208 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&mut self); }
    impl Tr for S { fn m(&mut self) { println!("c208 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &Box<S>) { v.m(); }
    pub fn drive() { probe(&Box::new(S)); }
}
pub mod c209 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(&self) { println!("c209 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&mut self); }
    impl Tr for &S { fn m(&mut self) { println!("c209 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &Box<S>) { v.m(); }
    pub fn drive() { probe(&Box::new(S)); }
}
pub mod c210 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(&self) { println!("c210 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&mut self); }
    impl Tr for &mut S { fn m(&mut self) { println!("c210 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &Box<S>) { v.m(); }
    pub fn drive() { probe(&Box::new(S)); }
}
pub mod c211 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(&mut self) { println!("c211 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(self); }
    impl Tr for S { fn m(self) { println!("c211 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &Box<S>) { v.m(); }
    pub fn drive() { probe(&Box::new(S)); }
}
pub mod c212 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(&mut self) { println!("c212 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(self); }
    impl Tr for &S { fn m(self) { println!("c212 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &Box<S>) { v.m(); }
    pub fn drive() { probe(&Box::new(S)); }
}
pub mod c213 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(&mut self) { println!("c213 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&self); }
    impl Tr for S { fn m(&self) { println!("c213 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &Box<S>) { v.m(); }
    pub fn drive() { probe(&Box::new(S)); }
}
pub mod c214 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(&mut self) { println!("c214 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&self); }
    impl Tr for Box<S> { fn m(&self) { println!("c214 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &Box<S>) { v.m(); }
    pub fn drive() { probe(&Box::new(S)); }
}
pub mod c215 {
    #[derive(Clone, Copy)] pub struct S;
    pub trait Tr { fn m(self); }
    impl Tr for S { fn m(self) { println!("c215 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut Box<S>) { v.m(); }
    pub fn drive() { probe(&mut Box::new(S)); }
}
pub mod c216 {
    #[derive(Clone, Copy)] pub struct S;
    pub trait Tr { fn m(self); }
    impl Tr for &S { fn m(self) { println!("c216 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut Box<S>) { v.m(); }
    pub fn drive() { probe(&mut Box::new(S)); }
}
pub mod c217 {
    #[derive(Clone, Copy)] pub struct S;
    pub trait Tr { fn m(self); }
    impl Tr for &mut S { fn m(self) { println!("c217 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut Box<S>) { v.m(); }
    pub fn drive() { probe(&mut Box::new(S)); }
}
pub mod c218 {
    #[derive(Clone, Copy)] pub struct S;
    pub trait Tr { fn m(&self); }
    impl Tr for S { fn m(&self) { println!("c218 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut Box<S>) { v.m(); }
    pub fn drive() { probe(&mut Box::new(S)); }
}
pub mod c219 {
    #[derive(Clone, Copy)] pub struct S;
    pub trait Tr { fn m(&self); }
    impl Tr for Box<S> { fn m(&self) { println!("c219 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut Box<S>) { v.m(); }
    pub fn drive() { probe(&mut Box::new(S)); }
}
pub mod c220 {
    #[derive(Clone, Copy)] pub struct S;
    pub trait Tr { fn m(&mut self); }
    impl Tr for S { fn m(&mut self) { println!("c220 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut Box<S>) { v.m(); }
    pub fn drive() { probe(&mut Box::new(S)); }
}
pub mod c221 {
    #[derive(Clone, Copy)] pub struct S;
    pub trait Tr { fn m(&mut self); }
    impl Tr for Box<S> { fn m(&mut self) { println!("c221 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut Box<S>) { v.m(); }
    pub fn drive() { probe(&mut Box::new(S)); }
}
pub mod c222 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(self) { println!("c222 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(self); }
    impl Tr for S { fn m(self) { println!("c222 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut Box<S>) { v.m(); }
    pub fn drive() { probe(&mut Box::new(S)); }
}
pub mod c223 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(self) { println!("c223 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(self); }
    impl Tr for &S { fn m(self) { println!("c223 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut Box<S>) { v.m(); }
    pub fn drive() { probe(&mut Box::new(S)); }
}
pub mod c224 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(self) { println!("c224 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(self); }
    impl Tr for &mut S { fn m(self) { println!("c224 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut Box<S>) { v.m(); }
    pub fn drive() { probe(&mut Box::new(S)); }
}
pub mod c225 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(self) { println!("c225 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&self); }
    impl Tr for S { fn m(&self) { println!("c225 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut Box<S>) { v.m(); }
    pub fn drive() { probe(&mut Box::new(S)); }
}
pub mod c226 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(self) { println!("c226 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&self); }
    impl Tr for &S { fn m(&self) { println!("c226 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut Box<S>) { v.m(); }
    pub fn drive() { probe(&mut Box::new(S)); }
}
pub mod c227 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(self) { println!("c227 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&self); }
    impl Tr for &mut S { fn m(&self) { println!("c227 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut Box<S>) { v.m(); }
    pub fn drive() { probe(&mut Box::new(S)); }
}
pub mod c228 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(self) { println!("c228 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&self); }
    impl Tr for Box<S> { fn m(&self) { println!("c228 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut Box<S>) { v.m(); }
    pub fn drive() { probe(&mut Box::new(S)); }
}
pub mod c229 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(self) { println!("c229 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&mut self); }
    impl Tr for S { fn m(&mut self) { println!("c229 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut Box<S>) { v.m(); }
    pub fn drive() { probe(&mut Box::new(S)); }
}
pub mod c230 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(self) { println!("c230 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&mut self); }
    impl Tr for &S { fn m(&mut self) { println!("c230 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut Box<S>) { v.m(); }
    pub fn drive() { probe(&mut Box::new(S)); }
}
pub mod c231 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(self) { println!("c231 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&mut self); }
    impl Tr for &mut S { fn m(&mut self) { println!("c231 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut Box<S>) { v.m(); }
    pub fn drive() { probe(&mut Box::new(S)); }
}
pub mod c232 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(self) { println!("c232 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&mut self); }
    impl Tr for Box<S> { fn m(&mut self) { println!("c232 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut Box<S>) { v.m(); }
    pub fn drive() { probe(&mut Box::new(S)); }
}
pub mod c233 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(&self) { println!("c233 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(self); }
    impl Tr for S { fn m(self) { println!("c233 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut Box<S>) { v.m(); }
    pub fn drive() { probe(&mut Box::new(S)); }
}
pub mod c234 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(&self) { println!("c234 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(self); }
    impl Tr for &S { fn m(self) { println!("c234 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut Box<S>) { v.m(); }
    pub fn drive() { probe(&mut Box::new(S)); }
}
pub mod c235 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(&self) { println!("c235 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(self); }
    impl Tr for &mut S { fn m(self) { println!("c235 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut Box<S>) { v.m(); }
    pub fn drive() { probe(&mut Box::new(S)); }
}
pub mod c236 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(&self) { println!("c236 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&self); }
    impl Tr for S { fn m(&self) { println!("c236 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut Box<S>) { v.m(); }
    pub fn drive() { probe(&mut Box::new(S)); }
}
pub mod c237 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(&self) { println!("c237 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&self); }
    impl Tr for &S { fn m(&self) { println!("c237 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut Box<S>) { v.m(); }
    pub fn drive() { probe(&mut Box::new(S)); }
}
pub mod c238 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(&self) { println!("c238 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&self); }
    impl Tr for &mut S { fn m(&self) { println!("c238 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut Box<S>) { v.m(); }
    pub fn drive() { probe(&mut Box::new(S)); }
}
pub mod c239 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(&self) { println!("c239 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&self); }
    impl Tr for Box<S> { fn m(&self) { println!("c239 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut Box<S>) { v.m(); }
    pub fn drive() { probe(&mut Box::new(S)); }
}
pub mod c240 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(&self) { println!("c240 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&mut self); }
    impl Tr for S { fn m(&mut self) { println!("c240 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut Box<S>) { v.m(); }
    pub fn drive() { probe(&mut Box::new(S)); }
}
pub mod c241 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(&self) { println!("c241 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&mut self); }
    impl Tr for &S { fn m(&mut self) { println!("c241 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut Box<S>) { v.m(); }
    pub fn drive() { probe(&mut Box::new(S)); }
}
pub mod c242 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(&self) { println!("c242 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&mut self); }
    impl Tr for &mut S { fn m(&mut self) { println!("c242 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut Box<S>) { v.m(); }
    pub fn drive() { probe(&mut Box::new(S)); }
}
pub mod c243 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(&self) { println!("c243 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&mut self); }
    impl Tr for Box<S> { fn m(&mut self) { println!("c243 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut Box<S>) { v.m(); }
    pub fn drive() { probe(&mut Box::new(S)); }
}
pub mod c244 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(&mut self) { println!("c244 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(self); }
    impl Tr for S { fn m(self) { println!("c244 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut Box<S>) { v.m(); }
    pub fn drive() { probe(&mut Box::new(S)); }
}
pub mod c245 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(&mut self) { println!("c245 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(self); }
    impl Tr for &S { fn m(self) { println!("c245 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut Box<S>) { v.m(); }
    pub fn drive() { probe(&mut Box::new(S)); }
}
pub mod c246 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(&mut self) { println!("c246 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(self); }
    impl Tr for &mut S { fn m(self) { println!("c246 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut Box<S>) { v.m(); }
    pub fn drive() { probe(&mut Box::new(S)); }
}
pub mod c247 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(&mut self) { println!("c247 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&self); }
    impl Tr for S { fn m(&self) { println!("c247 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut Box<S>) { v.m(); }
    pub fn drive() { probe(&mut Box::new(S)); }
}
pub mod c248 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(&mut self) { println!("c248 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&self); }
    impl Tr for &S { fn m(&self) { println!("c248 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut Box<S>) { v.m(); }
    pub fn drive() { probe(&mut Box::new(S)); }
}
pub mod c249 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(&mut self) { println!("c249 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&self); }
    impl Tr for &mut S { fn m(&self) { println!("c249 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut Box<S>) { v.m(); }
    pub fn drive() { probe(&mut Box::new(S)); }
}
pub mod c250 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(&mut self) { println!("c250 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&self); }
    impl Tr for Box<S> { fn m(&self) { println!("c250 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut Box<S>) { v.m(); }
    pub fn drive() { probe(&mut Box::new(S)); }
}
pub mod c251 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(&mut self) { println!("c251 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&mut self); }
    impl Tr for S { fn m(&mut self) { println!("c251 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut Box<S>) { v.m(); }
    pub fn drive() { probe(&mut Box::new(S)); }
}
pub mod c252 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(&mut self) { println!("c252 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&mut self); }
    impl Tr for &S { fn m(&mut self) { println!("c252 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut Box<S>) { v.m(); }
    pub fn drive() { probe(&mut Box::new(S)); }
}
pub mod c253 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(&mut self) { println!("c253 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&mut self); }
    impl Tr for &mut S { fn m(&mut self) { println!("c253 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut Box<S>) { v.m(); }
    pub fn drive() { probe(&mut Box::new(S)); }
}
pub mod c254 {
    #[derive(Clone, Copy)] pub struct S;
    impl S { pub fn m(&mut self) { println!("c254 inh\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr { fn m(&mut self); }
    impl Tr for Box<S> { fn m(&mut self) { println!("c254 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut Box<S>) { v.m(); }
    pub fn drive() { probe(&mut Box::new(S)); }
}
fn main() {
    c0::drive();
    c1::drive();
    c2::drive();
    c3::drive();
    c4::drive();
    c5::drive();
    c6::drive();
    c7::drive();
    c8::drive();
    c9::drive();
    c10::drive();
    c11::drive();
    c12::drive();
    c13::drive();
    c14::drive();
    c15::drive();
    c16::drive();
    c17::drive();
    c18::drive();
    c19::drive();
    c20::drive();
    c21::drive();
    c22::drive();
    c23::drive();
    c24::drive();
    c25::drive();
    c26::drive();
    c27::drive();
    c28::drive();
    c29::drive();
    c30::drive();
    c31::drive();
    c32::drive();
    c33::drive();
    c34::drive();
    c35::drive();
    c36::drive();
    c37::drive();
    c38::drive();
    c39::drive();
    c40::drive();
    c41::drive();
    c42::drive();
    c43::drive();
    c44::drive();
    c45::drive();
    c46::drive();
    c47::drive();
    c48::drive();
    c49::drive();
    c50::drive();
    c51::drive();
    c52::drive();
    c53::drive();
    c54::drive();
    c55::drive();
    c56::drive();
    c57::drive();
    c58::drive();
    c59::drive();
    c60::drive();
    c61::drive();
    c62::drive();
    c63::drive();
    c64::drive();
    c65::drive();
    c66::drive();
    c67::drive();
    c68::drive();
    c69::drive();
    c70::drive();
    c71::drive();
    c72::drive();
    c73::drive();
    c74::drive();
    c75::drive();
    c76::drive();
    c77::drive();
    c78::drive();
    c79::drive();
    c80::drive();
    c81::drive();
    c82::drive();
    c83::drive();
    c84::drive();
    c85::drive();
    c86::drive();
    c87::drive();
    c88::drive();
    c89::drive();
    c90::drive();
    c91::drive();
    c92::drive();
    c93::drive();
    c94::drive();
    c95::drive();
    c96::drive();
    c97::drive();
    c98::drive();
    c99::drive();
    c100::drive();
    c101::drive();
    c102::drive();
    c103::drive();
    c104::drive();
    c105::drive();
    c106::drive();
    c107::drive();
    c108::drive();
    c109::drive();
    c110::drive();
    c111::drive();
    c112::drive();
    c113::drive();
    c114::drive();
    c115::drive();
    c116::drive();
    c117::drive();
    c118::drive();
    c119::drive();
    c120::drive();
    c121::drive();
    c122::drive();
    c123::drive();
    c124::drive();
    c125::drive();
    c126::drive();
    c127::drive();
    c128::drive();
    c129::drive();
    c130::drive();
    c131::drive();
    c132::drive();
    c133::drive();
    c134::drive();
    c135::drive();
    c136::drive();
    c137::drive();
    c138::drive();
    c139::drive();
    c140::drive();
    c141::drive();
    c142::drive();
    c143::drive();
    c144::drive();
    c145::drive();
    c146::drive();
    c147::drive();
    c148::drive();
    c149::drive();
    c150::drive();
    c151::drive();
    c152::drive();
    c153::drive();
    c154::drive();
    c155::drive();
    c156::drive();
    c157::drive();
    c158::drive();
    c159::drive();
    c160::drive();
    c161::drive();
    c162::drive();
    c163::drive();
    c164::drive();
    c165::drive();
    c166::drive();
    c167::drive();
    c168::drive();
    c169::drive();
    c170::drive();
    c171::drive();
    c172::drive();
    c173::drive();
    c174::drive();
    c175::drive();
    c176::drive();
    c177::drive();
    c178::drive();
    c179::drive();
    c180::drive();
    c181::drive();
    c182::drive();
    c183::drive();
    c184::drive();
    c185::drive();
    c186::drive();
    c187::drive();
    c188::drive();
    c189::drive();
    c190::drive();
    c191::drive();
    c192::drive();
    c193::drive();
    c194::drive();
    c195::drive();
    c196::drive();
    c197::drive();
    c198::drive();
    c199::drive();
    c200::drive();
    c201::drive();
    c202::drive();
    c203::drive();
    c204::drive();
    c205::drive();
    c206::drive();
    c207::drive();
    c208::drive();
    c209::drive();
    c210::drive();
    c211::drive();
    c212::drive();
    c213::drive();
    c214::drive();
    c215::drive();
    c216::drive();
    c217::drive();
    c218::drive();
    c219::drive();
    c220::drive();
    c221::drive();
    c222::drive();
    c223::drive();
    c224::drive();
    c225::drive();
    c226::drive();
    c227::drive();
    c228::drive();
    c229::drive();
    c230::drive();
    c231::drive();
    c232::drive();
    c233::drive();
    c234::drive();
    c235::drive();
    c236::drive();
    c237::drive();
    c238::drive();
    c239::drive();
    c240::drive();
    c241::drive();
    c242::drive();
    c243::drive();
    c244::drive();
    c245::drive();
    c246::drive();
    c247::drive();
    c248::drive();
    c249::drive();
    c250::drive();
    c251::drive();
    c252::drive();
    c253::drive();
    c254::drive();
}
