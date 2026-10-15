pub mod c0 {
    #[derive(Clone, Copy)] pub struct S0;
    pub trait Tr0 { fn m(self); }
    impl Tr0 for S0 { fn m(self) { println!("c0 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe() { let mut v = S0; v.m(); }
    pub fn drive() { probe(); }
}
pub mod c1 {
    #[derive(Clone, Copy)] pub struct S1;
    pub trait Tr1 { fn m(self); }
    impl Tr1 for &S1 { fn m(self) { println!("c1 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe() { let mut v = S1; v.m(); }
    pub fn drive() { probe(); }
}
pub mod c2 {
    #[derive(Clone, Copy)] pub struct S2;
    pub trait Tr2 { fn m(self); }
    impl Tr2 for &mut S2 { fn m(self) { println!("c2 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe() { let mut v = S2; v.m(); }
    pub fn drive() { probe(); }
}
pub mod c3 {
    #[derive(Clone, Copy)] pub struct S3;
    pub trait Tr3 { fn m(&self); }
    impl Tr3 for S3 { fn m(&self) { println!("c3 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe() { let mut v = S3; v.m(); }
    pub fn drive() { probe(); }
}
pub mod c4 {
    #[derive(Clone, Copy)] pub struct S4;
    pub trait Tr4 { fn m(&mut self); }
    impl Tr4 for S4 { fn m(&mut self) { println!("c4 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe() { let mut v = S4; v.m(); }
    pub fn drive() { probe(); }
}
pub mod c5 {
    #[derive(Clone, Copy)] pub struct S5;
    impl S5 { pub fn m(self) { println!("c5 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr5 { fn m(self); }
    impl Tr5 for S5 { fn m(self) { println!("c5 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe() { let mut v = S5; v.m(); }
    pub fn drive() { probe(); }
}
pub mod c6 {
    #[derive(Clone, Copy)] pub struct S6;
    impl S6 { pub fn m(self) { println!("c6 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr6 { fn m(self); }
    impl Tr6 for &S6 { fn m(self) { println!("c6 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe() { let mut v = S6; v.m(); }
    pub fn drive() { probe(); }
}
pub mod c7 {
    #[derive(Clone, Copy)] pub struct S7;
    impl S7 { pub fn m(self) { println!("c7 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr7 { fn m(self); }
    impl Tr7 for &mut S7 { fn m(self) { println!("c7 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe() { let mut v = S7; v.m(); }
    pub fn drive() { probe(); }
}
pub mod c8 {
    #[derive(Clone, Copy)] pub struct S8;
    impl S8 { pub fn m(self) { println!("c8 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr8 { fn m(self); }
    impl Tr8 for Box<S8> { fn m(self) { println!("c8 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe() { let mut v = S8; v.m(); }
    pub fn drive() { probe(); }
}
pub mod c9 {
    #[derive(Clone, Copy)] pub struct S9;
    impl S9 { pub fn m(self) { println!("c9 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr9 { fn m(&self); }
    impl Tr9 for S9 { fn m(&self) { println!("c9 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe() { let mut v = S9; v.m(); }
    pub fn drive() { probe(); }
}
pub mod c10 {
    #[derive(Clone, Copy)] pub struct S10;
    impl S10 { pub fn m(self) { println!("c10 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr10 { fn m(&self); }
    impl Tr10 for &S10 { fn m(&self) { println!("c10 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe() { let mut v = S10; v.m(); }
    pub fn drive() { probe(); }
}
pub mod c11 {
    #[derive(Clone, Copy)] pub struct S11;
    impl S11 { pub fn m(self) { println!("c11 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr11 { fn m(&self); }
    impl Tr11 for &mut S11 { fn m(&self) { println!("c11 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe() { let mut v = S11; v.m(); }
    pub fn drive() { probe(); }
}
pub mod c12 {
    #[derive(Clone, Copy)] pub struct S12;
    impl S12 { pub fn m(self) { println!("c12 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr12 { fn m(&self); }
    impl Tr12 for Box<S12> { fn m(&self) { println!("c12 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe() { let mut v = S12; v.m(); }
    pub fn drive() { probe(); }
}
pub mod c13 {
    #[derive(Clone, Copy)] pub struct S13;
    impl S13 { pub fn m(self) { println!("c13 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr13 { fn m(&mut self); }
    impl Tr13 for S13 { fn m(&mut self) { println!("c13 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe() { let mut v = S13; v.m(); }
    pub fn drive() { probe(); }
}
pub mod c14 {
    #[derive(Clone, Copy)] pub struct S14;
    impl S14 { pub fn m(self) { println!("c14 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr14 { fn m(&mut self); }
    impl Tr14 for &S14 { fn m(&mut self) { println!("c14 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe() { let mut v = S14; v.m(); }
    pub fn drive() { probe(); }
}
pub mod c15 {
    #[derive(Clone, Copy)] pub struct S15;
    impl S15 { pub fn m(self) { println!("c15 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr15 { fn m(&mut self); }
    impl Tr15 for &mut S15 { fn m(&mut self) { println!("c15 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe() { let mut v = S15; v.m(); }
    pub fn drive() { probe(); }
}
pub mod c16 {
    #[derive(Clone, Copy)] pub struct S16;
    impl S16 { pub fn m(self) { println!("c16 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr16 { fn m(&mut self); }
    impl Tr16 for Box<S16> { fn m(&mut self) { println!("c16 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe() { let mut v = S16; v.m(); }
    pub fn drive() { probe(); }
}
pub mod c17 {
    #[derive(Clone, Copy)] pub struct S17;
    impl S17 { pub fn m(&self) { println!("c17 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr17 { fn m(self); }
    impl Tr17 for S17 { fn m(self) { println!("c17 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe() { let mut v = S17; v.m(); }
    pub fn drive() { probe(); }
}
pub mod c18 {
    #[derive(Clone, Copy)] pub struct S18;
    impl S18 { pub fn m(&self) { println!("c18 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr18 { fn m(self); }
    impl Tr18 for &S18 { fn m(self) { println!("c18 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe() { let mut v = S18; v.m(); }
    pub fn drive() { probe(); }
}
pub mod c19 {
    #[derive(Clone, Copy)] pub struct S19;
    impl S19 { pub fn m(&self) { println!("c19 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr19 { fn m(self); }
    impl Tr19 for &mut S19 { fn m(self) { println!("c19 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe() { let mut v = S19; v.m(); }
    pub fn drive() { probe(); }
}
pub mod c20 {
    #[derive(Clone, Copy)] pub struct S20;
    impl S20 { pub fn m(&self) { println!("c20 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr20 { fn m(self); }
    impl Tr20 for Box<S20> { fn m(self) { println!("c20 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe() { let mut v = S20; v.m(); }
    pub fn drive() { probe(); }
}
pub mod c21 {
    #[derive(Clone, Copy)] pub struct S21;
    impl S21 { pub fn m(&self) { println!("c21 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr21 { fn m(&self); }
    impl Tr21 for S21 { fn m(&self) { println!("c21 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe() { let mut v = S21; v.m(); }
    pub fn drive() { probe(); }
}
pub mod c22 {
    #[derive(Clone, Copy)] pub struct S22;
    impl S22 { pub fn m(&self) { println!("c22 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr22 { fn m(&self); }
    impl Tr22 for &S22 { fn m(&self) { println!("c22 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe() { let mut v = S22; v.m(); }
    pub fn drive() { probe(); }
}
pub mod c23 {
    #[derive(Clone, Copy)] pub struct S23;
    impl S23 { pub fn m(&self) { println!("c23 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr23 { fn m(&self); }
    impl Tr23 for &mut S23 { fn m(&self) { println!("c23 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe() { let mut v = S23; v.m(); }
    pub fn drive() { probe(); }
}
pub mod c24 {
    #[derive(Clone, Copy)] pub struct S24;
    impl S24 { pub fn m(&self) { println!("c24 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr24 { fn m(&self); }
    impl Tr24 for Box<S24> { fn m(&self) { println!("c24 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe() { let mut v = S24; v.m(); }
    pub fn drive() { probe(); }
}
pub mod c25 {
    #[derive(Clone, Copy)] pub struct S25;
    impl S25 { pub fn m(&self) { println!("c25 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr25 { fn m(&mut self); }
    impl Tr25 for S25 { fn m(&mut self) { println!("c25 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe() { let mut v = S25; v.m(); }
    pub fn drive() { probe(); }
}
pub mod c26 {
    #[derive(Clone, Copy)] pub struct S26;
    impl S26 { pub fn m(&self) { println!("c26 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr26 { fn m(&mut self); }
    impl Tr26 for &S26 { fn m(&mut self) { println!("c26 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe() { let mut v = S26; v.m(); }
    pub fn drive() { probe(); }
}
pub mod c27 {
    #[derive(Clone, Copy)] pub struct S27;
    impl S27 { pub fn m(&self) { println!("c27 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr27 { fn m(&mut self); }
    impl Tr27 for &mut S27 { fn m(&mut self) { println!("c27 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe() { let mut v = S27; v.m(); }
    pub fn drive() { probe(); }
}
pub mod c28 {
    #[derive(Clone, Copy)] pub struct S28;
    impl S28 { pub fn m(&self) { println!("c28 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr28 { fn m(&mut self); }
    impl Tr28 for Box<S28> { fn m(&mut self) { println!("c28 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe() { let mut v = S28; v.m(); }
    pub fn drive() { probe(); }
}
pub mod c29 {
    #[derive(Clone, Copy)] pub struct S29;
    impl S29 { pub fn m(&mut self) { println!("c29 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr29 { fn m(self); }
    impl Tr29 for S29 { fn m(self) { println!("c29 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe() { let mut v = S29; v.m(); }
    pub fn drive() { probe(); }
}
pub mod c30 {
    #[derive(Clone, Copy)] pub struct S30;
    impl S30 { pub fn m(&mut self) { println!("c30 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr30 { fn m(self); }
    impl Tr30 for &S30 { fn m(self) { println!("c30 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe() { let mut v = S30; v.m(); }
    pub fn drive() { probe(); }
}
pub mod c31 {
    #[derive(Clone, Copy)] pub struct S31;
    impl S31 { pub fn m(&mut self) { println!("c31 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr31 { fn m(self); }
    impl Tr31 for &mut S31 { fn m(self) { println!("c31 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe() { let mut v = S31; v.m(); }
    pub fn drive() { probe(); }
}
pub mod c32 {
    #[derive(Clone, Copy)] pub struct S32;
    impl S32 { pub fn m(&mut self) { println!("c32 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr32 { fn m(self); }
    impl Tr32 for Box<S32> { fn m(self) { println!("c32 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe() { let mut v = S32; v.m(); }
    pub fn drive() { probe(); }
}
pub mod c33 {
    #[derive(Clone, Copy)] pub struct S33;
    impl S33 { pub fn m(&mut self) { println!("c33 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr33 { fn m(&self); }
    impl Tr33 for S33 { fn m(&self) { println!("c33 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe() { let mut v = S33; v.m(); }
    pub fn drive() { probe(); }
}
pub mod c34 {
    #[derive(Clone, Copy)] pub struct S34;
    impl S34 { pub fn m(&mut self) { println!("c34 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr34 { fn m(&self); }
    impl Tr34 for &S34 { fn m(&self) { println!("c34 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe() { let mut v = S34; v.m(); }
    pub fn drive() { probe(); }
}
pub mod c35 {
    #[derive(Clone, Copy)] pub struct S35;
    impl S35 { pub fn m(&mut self) { println!("c35 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr35 { fn m(&self); }
    impl Tr35 for &mut S35 { fn m(&self) { println!("c35 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe() { let mut v = S35; v.m(); }
    pub fn drive() { probe(); }
}
pub mod c36 {
    #[derive(Clone, Copy)] pub struct S36;
    impl S36 { pub fn m(&mut self) { println!("c36 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr36 { fn m(&self); }
    impl Tr36 for Box<S36> { fn m(&self) { println!("c36 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe() { let mut v = S36; v.m(); }
    pub fn drive() { probe(); }
}
pub mod c37 {
    #[derive(Clone, Copy)] pub struct S37;
    impl S37 { pub fn m(&mut self) { println!("c37 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr37 { fn m(&mut self); }
    impl Tr37 for S37 { fn m(&mut self) { println!("c37 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe() { let mut v = S37; v.m(); }
    pub fn drive() { probe(); }
}
pub mod c38 {
    #[derive(Clone, Copy)] pub struct S38;
    impl S38 { pub fn m(&mut self) { println!("c38 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr38 { fn m(&mut self); }
    impl Tr38 for &S38 { fn m(&mut self) { println!("c38 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe() { let mut v = S38; v.m(); }
    pub fn drive() { probe(); }
}
pub mod c39 {
    #[derive(Clone, Copy)] pub struct S39;
    impl S39 { pub fn m(&mut self) { println!("c39 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr39 { fn m(&mut self); }
    impl Tr39 for &mut S39 { fn m(&mut self) { println!("c39 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe() { let mut v = S39; v.m(); }
    pub fn drive() { probe(); }
}
pub mod c40 {
    #[derive(Clone, Copy)] pub struct S40;
    impl S40 { pub fn m(&mut self) { println!("c40 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr40 { fn m(&mut self); }
    impl Tr40 for Box<S40> { fn m(&mut self) { println!("c40 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe() { let mut v = S40; v.m(); }
    pub fn drive() { probe(); }
}
pub mod c41 {
    #[derive(Clone, Copy)] pub struct S41;
    pub trait Fa41 { fn m(self); }
    impl Fa41 for S41 { fn m(self) { println!("c41 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr41 { fn m(self); }
    impl Tr41 for &S41 { fn m(self) { println!("c41 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe() { let mut v = S41; v.m(); }
    pub fn drive() { probe(); }
}
pub mod c42 {
    #[derive(Clone, Copy)] pub struct S42;
    pub trait Fa42 { fn m(self); }
    impl Fa42 for S42 { fn m(self) { println!("c42 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr42 { fn m(self); }
    impl Tr42 for &mut S42 { fn m(self) { println!("c42 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe() { let mut v = S42; v.m(); }
    pub fn drive() { probe(); }
}
pub mod c43 {
    #[derive(Clone, Copy)] pub struct S43;
    pub trait Fa43 { fn m(self); }
    impl Fa43 for S43 { fn m(self) { println!("c43 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr43 { fn m(self); }
    impl Tr43 for Box<S43> { fn m(self) { println!("c43 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe() { let mut v = S43; v.m(); }
    pub fn drive() { probe(); }
}
pub mod c44 {
    #[derive(Clone, Copy)] pub struct S44;
    pub trait Fa44 { fn m(self); }
    impl Fa44 for S44 { fn m(self) { println!("c44 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr44 { fn m(&self); }
    impl Tr44 for S44 { fn m(&self) { println!("c44 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe() { let mut v = S44; v.m(); }
    pub fn drive() { probe(); }
}
pub mod c45 {
    #[derive(Clone, Copy)] pub struct S45;
    pub trait Fa45 { fn m(self); }
    impl Fa45 for S45 { fn m(self) { println!("c45 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr45 { fn m(&self); }
    impl Tr45 for &S45 { fn m(&self) { println!("c45 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe() { let mut v = S45; v.m(); }
    pub fn drive() { probe(); }
}
pub mod c46 {
    #[derive(Clone, Copy)] pub struct S46;
    pub trait Fa46 { fn m(self); }
    impl Fa46 for S46 { fn m(self) { println!("c46 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr46 { fn m(&self); }
    impl Tr46 for &mut S46 { fn m(&self) { println!("c46 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe() { let mut v = S46; v.m(); }
    pub fn drive() { probe(); }
}
pub mod c47 {
    #[derive(Clone, Copy)] pub struct S47;
    pub trait Fa47 { fn m(self); }
    impl Fa47 for S47 { fn m(self) { println!("c47 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr47 { fn m(&self); }
    impl Tr47 for Box<S47> { fn m(&self) { println!("c47 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe() { let mut v = S47; v.m(); }
    pub fn drive() { probe(); }
}
pub mod c48 {
    #[derive(Clone, Copy)] pub struct S48;
    pub trait Fa48 { fn m(self); }
    impl Fa48 for S48 { fn m(self) { println!("c48 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr48 { fn m(&mut self); }
    impl Tr48 for S48 { fn m(&mut self) { println!("c48 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe() { let mut v = S48; v.m(); }
    pub fn drive() { probe(); }
}
pub mod c49 {
    #[derive(Clone, Copy)] pub struct S49;
    pub trait Fa49 { fn m(self); }
    impl Fa49 for S49 { fn m(self) { println!("c49 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr49 { fn m(&mut self); }
    impl Tr49 for &S49 { fn m(&mut self) { println!("c49 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe() { let mut v = S49; v.m(); }
    pub fn drive() { probe(); }
}
pub mod c50 {
    #[derive(Clone, Copy)] pub struct S50;
    pub trait Fa50 { fn m(self); }
    impl Fa50 for S50 { fn m(self) { println!("c50 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr50 { fn m(&mut self); }
    impl Tr50 for &mut S50 { fn m(&mut self) { println!("c50 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe() { let mut v = S50; v.m(); }
    pub fn drive() { probe(); }
}
pub mod c51 {
    #[derive(Clone, Copy)] pub struct S51;
    pub trait Fa51 { fn m(self); }
    impl Fa51 for S51 { fn m(self) { println!("c51 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr51 { fn m(&mut self); }
    impl Tr51 for Box<S51> { fn m(&mut self) { println!("c51 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe() { let mut v = S51; v.m(); }
    pub fn drive() { probe(); }
}
pub mod c52 {
    #[derive(Clone, Copy)] pub struct S52;
    pub trait Fa52 { fn m(&self); }
    impl Fa52 for S52 { fn m(&self) { println!("c52 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr52 { fn m(self); }
    impl Tr52 for S52 { fn m(self) { println!("c52 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe() { let mut v = S52; v.m(); }
    pub fn drive() { probe(); }
}
pub mod c53 {
    #[derive(Clone, Copy)] pub struct S53;
    pub trait Fa53 { fn m(&self); }
    impl Fa53 for S53 { fn m(&self) { println!("c53 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr53 { fn m(self); }
    impl Tr53 for &mut S53 { fn m(self) { println!("c53 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe() { let mut v = S53; v.m(); }
    pub fn drive() { probe(); }
}
pub mod c54 {
    #[derive(Clone, Copy)] pub struct S54;
    pub trait Fa54 { fn m(&self); }
    impl Fa54 for S54 { fn m(&self) { println!("c54 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr54 { fn m(self); }
    impl Tr54 for Box<S54> { fn m(self) { println!("c54 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe() { let mut v = S54; v.m(); }
    pub fn drive() { probe(); }
}
pub mod c55 {
    #[derive(Clone, Copy)] pub struct S55;
    pub trait Fa55 { fn m(&self); }
    impl Fa55 for S55 { fn m(&self) { println!("c55 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr55 { fn m(&self); }
    impl Tr55 for &S55 { fn m(&self) { println!("c55 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe() { let mut v = S55; v.m(); }
    pub fn drive() { probe(); }
}
pub mod c56 {
    #[derive(Clone, Copy)] pub struct S56;
    pub trait Fa56 { fn m(&self); }
    impl Fa56 for S56 { fn m(&self) { println!("c56 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr56 { fn m(&self); }
    impl Tr56 for &mut S56 { fn m(&self) { println!("c56 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe() { let mut v = S56; v.m(); }
    pub fn drive() { probe(); }
}
pub mod c57 {
    #[derive(Clone, Copy)] pub struct S57;
    pub trait Fa57 { fn m(&self); }
    impl Fa57 for S57 { fn m(&self) { println!("c57 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr57 { fn m(&self); }
    impl Tr57 for Box<S57> { fn m(&self) { println!("c57 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe() { let mut v = S57; v.m(); }
    pub fn drive() { probe(); }
}
pub mod c58 {
    #[derive(Clone, Copy)] pub struct S58;
    pub trait Fa58 { fn m(&self); }
    impl Fa58 for S58 { fn m(&self) { println!("c58 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr58 { fn m(&mut self); }
    impl Tr58 for S58 { fn m(&mut self) { println!("c58 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe() { let mut v = S58; v.m(); }
    pub fn drive() { probe(); }
}
pub mod c59 {
    #[derive(Clone, Copy)] pub struct S59;
    pub trait Fa59 { fn m(&self); }
    impl Fa59 for S59 { fn m(&self) { println!("c59 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr59 { fn m(&mut self); }
    impl Tr59 for &S59 { fn m(&mut self) { println!("c59 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe() { let mut v = S59; v.m(); }
    pub fn drive() { probe(); }
}
pub mod c60 {
    #[derive(Clone, Copy)] pub struct S60;
    pub trait Fa60 { fn m(&self); }
    impl Fa60 for S60 { fn m(&self) { println!("c60 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr60 { fn m(&mut self); }
    impl Tr60 for &mut S60 { fn m(&mut self) { println!("c60 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe() { let mut v = S60; v.m(); }
    pub fn drive() { probe(); }
}
pub mod c61 {
    #[derive(Clone, Copy)] pub struct S61;
    pub trait Fa61 { fn m(&self); }
    impl Fa61 for S61 { fn m(&self) { println!("c61 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr61 { fn m(&mut self); }
    impl Tr61 for Box<S61> { fn m(&mut self) { println!("c61 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe() { let mut v = S61; v.m(); }
    pub fn drive() { probe(); }
}
pub mod c62 {
    #[derive(Clone, Copy)] pub struct S62;
    pub trait Fa62 { fn m(&mut self); }
    impl Fa62 for S62 { fn m(&mut self) { println!("c62 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr62 { fn m(self); }
    impl Tr62 for S62 { fn m(self) { println!("c62 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe() { let mut v = S62; v.m(); }
    pub fn drive() { probe(); }
}
pub mod c63 {
    #[derive(Clone, Copy)] pub struct S63;
    pub trait Fa63 { fn m(&mut self); }
    impl Fa63 for S63 { fn m(&mut self) { println!("c63 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr63 { fn m(self); }
    impl Tr63 for &S63 { fn m(self) { println!("c63 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe() { let mut v = S63; v.m(); }
    pub fn drive() { probe(); }
}
pub mod c64 {
    #[derive(Clone, Copy)] pub struct S64;
    pub trait Fa64 { fn m(&mut self); }
    impl Fa64 for S64 { fn m(&mut self) { println!("c64 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr64 { fn m(self); }
    impl Tr64 for Box<S64> { fn m(self) { println!("c64 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe() { let mut v = S64; v.m(); }
    pub fn drive() { probe(); }
}
pub mod c65 {
    #[derive(Clone, Copy)] pub struct S65;
    pub trait Fa65 { fn m(&mut self); }
    impl Fa65 for S65 { fn m(&mut self) { println!("c65 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr65 { fn m(&self); }
    impl Tr65 for S65 { fn m(&self) { println!("c65 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe() { let mut v = S65; v.m(); }
    pub fn drive() { probe(); }
}
pub mod c66 {
    #[derive(Clone, Copy)] pub struct S66;
    pub trait Fa66 { fn m(&mut self); }
    impl Fa66 for S66 { fn m(&mut self) { println!("c66 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr66 { fn m(&self); }
    impl Tr66 for &S66 { fn m(&self) { println!("c66 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe() { let mut v = S66; v.m(); }
    pub fn drive() { probe(); }
}
pub mod c67 {
    #[derive(Clone, Copy)] pub struct S67;
    pub trait Fa67 { fn m(&mut self); }
    impl Fa67 for S67 { fn m(&mut self) { println!("c67 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr67 { fn m(&self); }
    impl Tr67 for &mut S67 { fn m(&self) { println!("c67 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe() { let mut v = S67; v.m(); }
    pub fn drive() { probe(); }
}
pub mod c68 {
    #[derive(Clone, Copy)] pub struct S68;
    pub trait Fa68 { fn m(&mut self); }
    impl Fa68 for S68 { fn m(&mut self) { println!("c68 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr68 { fn m(&self); }
    impl Tr68 for Box<S68> { fn m(&self) { println!("c68 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe() { let mut v = S68; v.m(); }
    pub fn drive() { probe(); }
}
pub mod c69 {
    #[derive(Clone, Copy)] pub struct S69;
    pub trait Fa69 { fn m(&mut self); }
    impl Fa69 for S69 { fn m(&mut self) { println!("c69 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr69 { fn m(&mut self); }
    impl Tr69 for &S69 { fn m(&mut self) { println!("c69 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe() { let mut v = S69; v.m(); }
    pub fn drive() { probe(); }
}
pub mod c70 {
    #[derive(Clone, Copy)] pub struct S70;
    pub trait Fa70 { fn m(&mut self); }
    impl Fa70 for S70 { fn m(&mut self) { println!("c70 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr70 { fn m(&mut self); }
    impl Tr70 for &mut S70 { fn m(&mut self) { println!("c70 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe() { let mut v = S70; v.m(); }
    pub fn drive() { probe(); }
}
pub mod c71 {
    #[derive(Clone, Copy)] pub struct S71;
    pub trait Fa71 { fn m(&mut self); }
    impl Fa71 for S71 { fn m(&mut self) { println!("c71 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr71 { fn m(&mut self); }
    impl Tr71 for Box<S71> { fn m(&mut self) { println!("c71 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe() { let mut v = S71; v.m(); }
    pub fn drive() { probe(); }
}
pub mod c72 {
    #[derive(Clone, Copy)] pub struct S72;
    pub trait Fa72 { fn m(self); }
    impl Fa72 for &S72 { fn m(self) { println!("c72 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr72 { fn m(self); }
    impl Tr72 for S72 { fn m(self) { println!("c72 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe() { let mut v = S72; v.m(); }
    pub fn drive() { probe(); }
}
pub mod c73 {
    #[derive(Clone, Copy)] pub struct S73;
    pub trait Fa73 { fn m(self); }
    impl Fa73 for &S73 { fn m(self) { println!("c73 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr73 { fn m(self); }
    impl Tr73 for &mut S73 { fn m(self) { println!("c73 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe() { let mut v = S73; v.m(); }
    pub fn drive() { probe(); }
}
pub mod c74 {
    #[derive(Clone, Copy)] pub struct S74;
    pub trait Fa74 { fn m(self); }
    impl Fa74 for &S74 { fn m(self) { println!("c74 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr74 { fn m(self); }
    impl Tr74 for Box<S74> { fn m(self) { println!("c74 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe() { let mut v = S74; v.m(); }
    pub fn drive() { probe(); }
}
pub mod c75 {
    #[derive(Clone, Copy)] pub struct S75;
    pub trait Fa75 { fn m(self); }
    impl Fa75 for &S75 { fn m(self) { println!("c75 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr75 { fn m(&self); }
    impl Tr75 for &S75 { fn m(&self) { println!("c75 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe() { let mut v = S75; v.m(); }
    pub fn drive() { probe(); }
}
pub mod c76 {
    #[derive(Clone, Copy)] pub struct S76;
    pub trait Fa76 { fn m(self); }
    impl Fa76 for &S76 { fn m(self) { println!("c76 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr76 { fn m(&self); }
    impl Tr76 for &mut S76 { fn m(&self) { println!("c76 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe() { let mut v = S76; v.m(); }
    pub fn drive() { probe(); }
}
pub mod c77 {
    #[derive(Clone, Copy)] pub struct S77;
    pub trait Fa77 { fn m(self); }
    impl Fa77 for &S77 { fn m(self) { println!("c77 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr77 { fn m(&self); }
    impl Tr77 for Box<S77> { fn m(&self) { println!("c77 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe() { let mut v = S77; v.m(); }
    pub fn drive() { probe(); }
}
pub mod c78 {
    #[derive(Clone, Copy)] pub struct S78;
    pub trait Fa78 { fn m(self); }
    impl Fa78 for &S78 { fn m(self) { println!("c78 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr78 { fn m(&mut self); }
    impl Tr78 for S78 { fn m(&mut self) { println!("c78 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe() { let mut v = S78; v.m(); }
    pub fn drive() { probe(); }
}
pub mod c79 {
    #[derive(Clone, Copy)] pub struct S79;
    pub trait Fa79 { fn m(self); }
    impl Fa79 for &S79 { fn m(self) { println!("c79 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr79 { fn m(&mut self); }
    impl Tr79 for &S79 { fn m(&mut self) { println!("c79 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe() { let mut v = S79; v.m(); }
    pub fn drive() { probe(); }
}
pub mod c80 {
    #[derive(Clone, Copy)] pub struct S80;
    pub trait Fa80 { fn m(self); }
    impl Fa80 for &S80 { fn m(self) { println!("c80 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr80 { fn m(&mut self); }
    impl Tr80 for &mut S80 { fn m(&mut self) { println!("c80 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe() { let mut v = S80; v.m(); }
    pub fn drive() { probe(); }
}
pub mod c81 {
    #[derive(Clone, Copy)] pub struct S81;
    pub trait Fa81 { fn m(self); }
    impl Fa81 for &S81 { fn m(self) { println!("c81 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr81 { fn m(&mut self); }
    impl Tr81 for Box<S81> { fn m(&mut self) { println!("c81 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe() { let mut v = S81; v.m(); }
    pub fn drive() { probe(); }
}
pub mod c82 {
    #[derive(Clone, Copy)] pub struct S82;
    pub trait Fa82 { fn m(self); }
    impl Fa82 for &mut S82 { fn m(self) { println!("c82 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr82 { fn m(self); }
    impl Tr82 for S82 { fn m(self) { println!("c82 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe() { let mut v = S82; v.m(); }
    pub fn drive() { probe(); }
}
pub mod c83 {
    #[derive(Clone, Copy)] pub struct S83;
    pub trait Fa83 { fn m(self); }
    impl Fa83 for &mut S83 { fn m(self) { println!("c83 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr83 { fn m(self); }
    impl Tr83 for &S83 { fn m(self) { println!("c83 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe() { let mut v = S83; v.m(); }
    pub fn drive() { probe(); }
}
pub mod c84 {
    #[derive(Clone, Copy)] pub struct S84;
    pub trait Fa84 { fn m(self); }
    impl Fa84 for &mut S84 { fn m(self) { println!("c84 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr84 { fn m(self); }
    impl Tr84 for Box<S84> { fn m(self) { println!("c84 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe() { let mut v = S84; v.m(); }
    pub fn drive() { probe(); }
}
pub mod c85 {
    #[derive(Clone, Copy)] pub struct S85;
    pub trait Fa85 { fn m(self); }
    impl Fa85 for &mut S85 { fn m(self) { println!("c85 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr85 { fn m(&self); }
    impl Tr85 for S85 { fn m(&self) { println!("c85 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe() { let mut v = S85; v.m(); }
    pub fn drive() { probe(); }
}
pub mod c86 {
    #[derive(Clone, Copy)] pub struct S86;
    pub trait Fa86 { fn m(self); }
    impl Fa86 for &mut S86 { fn m(self) { println!("c86 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr86 { fn m(&self); }
    impl Tr86 for &S86 { fn m(&self) { println!("c86 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe() { let mut v = S86; v.m(); }
    pub fn drive() { probe(); }
}
pub mod c87 {
    #[derive(Clone, Copy)] pub struct S87;
    pub trait Fa87 { fn m(self); }
    impl Fa87 for &mut S87 { fn m(self) { println!("c87 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr87 { fn m(&self); }
    impl Tr87 for &mut S87 { fn m(&self) { println!("c87 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe() { let mut v = S87; v.m(); }
    pub fn drive() { probe(); }
}
pub mod c88 {
    #[derive(Clone, Copy)] pub struct S88;
    pub trait Fa88 { fn m(self); }
    impl Fa88 for &mut S88 { fn m(self) { println!("c88 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr88 { fn m(&self); }
    impl Tr88 for Box<S88> { fn m(&self) { println!("c88 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe() { let mut v = S88; v.m(); }
    pub fn drive() { probe(); }
}
pub mod c89 {
    #[derive(Clone, Copy)] pub struct S89;
    pub trait Fa89 { fn m(self); }
    impl Fa89 for &mut S89 { fn m(self) { println!("c89 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr89 { fn m(&mut self); }
    impl Tr89 for &S89 { fn m(&mut self) { println!("c89 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe() { let mut v = S89; v.m(); }
    pub fn drive() { probe(); }
}
pub mod c90 {
    #[derive(Clone, Copy)] pub struct S90;
    pub trait Fa90 { fn m(self); }
    impl Fa90 for &mut S90 { fn m(self) { println!("c90 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr90 { fn m(&mut self); }
    impl Tr90 for &mut S90 { fn m(&mut self) { println!("c90 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe() { let mut v = S90; v.m(); }
    pub fn drive() { probe(); }
}
pub mod c91 {
    #[derive(Clone, Copy)] pub struct S91;
    pub trait Fa91 { fn m(self); }
    impl Fa91 for &mut S91 { fn m(self) { println!("c91 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr91 { fn m(&mut self); }
    impl Tr91 for Box<S91> { fn m(&mut self) { println!("c91 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe() { let mut v = S91; v.m(); }
    pub fn drive() { probe(); }
}
pub mod c92 {
    #[derive(Clone, Copy)] pub struct S92;
    pub trait Tr92 { fn m(self); }
    impl Tr92 for S92 { fn m(self) { println!("c92 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &S92) { v.m(); }
    pub fn drive() { probe(&S92); }
}
pub mod c93 {
    #[derive(Clone, Copy)] pub struct S93;
    pub trait Tr93 { fn m(self); }
    impl Tr93 for &S93 { fn m(self) { println!("c93 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &S93) { v.m(); }
    pub fn drive() { probe(&S93); }
}
pub mod c94 {
    #[derive(Clone, Copy)] pub struct S94;
    pub trait Tr94 { fn m(&self); }
    impl Tr94 for S94 { fn m(&self) { println!("c94 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &S94) { v.m(); }
    pub fn drive() { probe(&S94); }
}
pub mod c95 {
    #[derive(Clone, Copy)] pub struct S95;
    pub trait Tr95 { fn m(&self); }
    impl Tr95 for &S95 { fn m(&self) { println!("c95 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &S95) { v.m(); }
    pub fn drive() { probe(&S95); }
}
pub mod c96 {
    #[derive(Clone, Copy)] pub struct S96;
    impl S96 { pub fn m(self) { println!("c96 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr96 { fn m(self); }
    impl Tr96 for S96 { fn m(self) { println!("c96 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &S96) { v.m(); }
    pub fn drive() { probe(&S96); }
}
pub mod c97 {
    #[derive(Clone, Copy)] pub struct S97;
    impl S97 { pub fn m(self) { println!("c97 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr97 { fn m(self); }
    impl Tr97 for &S97 { fn m(self) { println!("c97 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &S97) { v.m(); }
    pub fn drive() { probe(&S97); }
}
pub mod c98 {
    #[derive(Clone, Copy)] pub struct S98;
    impl S98 { pub fn m(self) { println!("c98 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr98 { fn m(self); }
    impl Tr98 for &mut S98 { fn m(self) { println!("c98 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &S98) { v.m(); }
    pub fn drive() { probe(&S98); }
}
pub mod c99 {
    #[derive(Clone, Copy)] pub struct S99;
    impl S99 { pub fn m(self) { println!("c99 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr99 { fn m(self); }
    impl Tr99 for Box<S99> { fn m(self) { println!("c99 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &S99) { v.m(); }
    pub fn drive() { probe(&S99); }
}
pub mod c100 {
    #[derive(Clone, Copy)] pub struct S100;
    impl S100 { pub fn m(self) { println!("c100 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr100 { fn m(&self); }
    impl Tr100 for S100 { fn m(&self) { println!("c100 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &S100) { v.m(); }
    pub fn drive() { probe(&S100); }
}
pub mod c101 {
    #[derive(Clone, Copy)] pub struct S101;
    impl S101 { pub fn m(self) { println!("c101 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr101 { fn m(&self); }
    impl Tr101 for &S101 { fn m(&self) { println!("c101 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &S101) { v.m(); }
    pub fn drive() { probe(&S101); }
}
pub mod c102 {
    #[derive(Clone, Copy)] pub struct S102;
    impl S102 { pub fn m(self) { println!("c102 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr102 { fn m(&self); }
    impl Tr102 for &mut S102 { fn m(&self) { println!("c102 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &S102) { v.m(); }
    pub fn drive() { probe(&S102); }
}
pub mod c103 {
    #[derive(Clone, Copy)] pub struct S103;
    impl S103 { pub fn m(self) { println!("c103 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr103 { fn m(&self); }
    impl Tr103 for Box<S103> { fn m(&self) { println!("c103 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &S103) { v.m(); }
    pub fn drive() { probe(&S103); }
}
pub mod c104 {
    #[derive(Clone, Copy)] pub struct S104;
    impl S104 { pub fn m(self) { println!("c104 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr104 { fn m(&mut self); }
    impl Tr104 for S104 { fn m(&mut self) { println!("c104 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &S104) { v.m(); }
    pub fn drive() { probe(&S104); }
}
pub mod c105 {
    #[derive(Clone, Copy)] pub struct S105;
    impl S105 { pub fn m(self) { println!("c105 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr105 { fn m(&mut self); }
    impl Tr105 for &mut S105 { fn m(&mut self) { println!("c105 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &S105) { v.m(); }
    pub fn drive() { probe(&S105); }
}
pub mod c106 {
    #[derive(Clone, Copy)] pub struct S106;
    impl S106 { pub fn m(self) { println!("c106 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr106 { fn m(&mut self); }
    impl Tr106 for Box<S106> { fn m(&mut self) { println!("c106 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &S106) { v.m(); }
    pub fn drive() { probe(&S106); }
}
pub mod c107 {
    #[derive(Clone, Copy)] pub struct S107;
    impl S107 { pub fn m(&self) { println!("c107 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr107 { fn m(self); }
    impl Tr107 for S107 { fn m(self) { println!("c107 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &S107) { v.m(); }
    pub fn drive() { probe(&S107); }
}
pub mod c108 {
    #[derive(Clone, Copy)] pub struct S108;
    impl S108 { pub fn m(&self) { println!("c108 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr108 { fn m(self); }
    impl Tr108 for &S108 { fn m(self) { println!("c108 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &S108) { v.m(); }
    pub fn drive() { probe(&S108); }
}
pub mod c109 {
    #[derive(Clone, Copy)] pub struct S109;
    impl S109 { pub fn m(&self) { println!("c109 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr109 { fn m(self); }
    impl Tr109 for &mut S109 { fn m(self) { println!("c109 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &S109) { v.m(); }
    pub fn drive() { probe(&S109); }
}
pub mod c110 {
    #[derive(Clone, Copy)] pub struct S110;
    impl S110 { pub fn m(&self) { println!("c110 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr110 { fn m(self); }
    impl Tr110 for Box<S110> { fn m(self) { println!("c110 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &S110) { v.m(); }
    pub fn drive() { probe(&S110); }
}
pub mod c111 {
    #[derive(Clone, Copy)] pub struct S111;
    impl S111 { pub fn m(&self) { println!("c111 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr111 { fn m(&self); }
    impl Tr111 for S111 { fn m(&self) { println!("c111 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &S111) { v.m(); }
    pub fn drive() { probe(&S111); }
}
pub mod c112 {
    #[derive(Clone, Copy)] pub struct S112;
    impl S112 { pub fn m(&self) { println!("c112 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr112 { fn m(&self); }
    impl Tr112 for &S112 { fn m(&self) { println!("c112 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &S112) { v.m(); }
    pub fn drive() { probe(&S112); }
}
pub mod c113 {
    #[derive(Clone, Copy)] pub struct S113;
    impl S113 { pub fn m(&self) { println!("c113 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr113 { fn m(&self); }
    impl Tr113 for &mut S113 { fn m(&self) { println!("c113 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &S113) { v.m(); }
    pub fn drive() { probe(&S113); }
}
pub mod c114 {
    #[derive(Clone, Copy)] pub struct S114;
    impl S114 { pub fn m(&self) { println!("c114 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr114 { fn m(&self); }
    impl Tr114 for Box<S114> { fn m(&self) { println!("c114 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &S114) { v.m(); }
    pub fn drive() { probe(&S114); }
}
pub mod c115 {
    #[derive(Clone, Copy)] pub struct S115;
    impl S115 { pub fn m(&self) { println!("c115 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr115 { fn m(&mut self); }
    impl Tr115 for S115 { fn m(&mut self) { println!("c115 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &S115) { v.m(); }
    pub fn drive() { probe(&S115); }
}
pub mod c116 {
    #[derive(Clone, Copy)] pub struct S116;
    impl S116 { pub fn m(&self) { println!("c116 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr116 { fn m(&mut self); }
    impl Tr116 for &S116 { fn m(&mut self) { println!("c116 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &S116) { v.m(); }
    pub fn drive() { probe(&S116); }
}
pub mod c117 {
    #[derive(Clone, Copy)] pub struct S117;
    impl S117 { pub fn m(&self) { println!("c117 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr117 { fn m(&mut self); }
    impl Tr117 for &mut S117 { fn m(&mut self) { println!("c117 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &S117) { v.m(); }
    pub fn drive() { probe(&S117); }
}
pub mod c118 {
    #[derive(Clone, Copy)] pub struct S118;
    impl S118 { pub fn m(&self) { println!("c118 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr118 { fn m(&mut self); }
    impl Tr118 for Box<S118> { fn m(&mut self) { println!("c118 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &S118) { v.m(); }
    pub fn drive() { probe(&S118); }
}
pub mod c119 {
    #[derive(Clone, Copy)] pub struct S119;
    impl S119 { pub fn m(&mut self) { println!("c119 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr119 { fn m(self); }
    impl Tr119 for S119 { fn m(self) { println!("c119 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &S119) { v.m(); }
    pub fn drive() { probe(&S119); }
}
pub mod c120 {
    #[derive(Clone, Copy)] pub struct S120;
    impl S120 { pub fn m(&mut self) { println!("c120 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr120 { fn m(self); }
    impl Tr120 for &S120 { fn m(self) { println!("c120 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &S120) { v.m(); }
    pub fn drive() { probe(&S120); }
}
pub mod c121 {
    #[derive(Clone, Copy)] pub struct S121;
    impl S121 { pub fn m(&mut self) { println!("c121 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr121 { fn m(&self); }
    impl Tr121 for S121 { fn m(&self) { println!("c121 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &S121) { v.m(); }
    pub fn drive() { probe(&S121); }
}
pub mod c122 {
    #[derive(Clone, Copy)] pub struct S122;
    impl S122 { pub fn m(&mut self) { println!("c122 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr122 { fn m(&self); }
    impl Tr122 for &S122 { fn m(&self) { println!("c122 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &S122) { v.m(); }
    pub fn drive() { probe(&S122); }
}
pub mod c123 {
    #[derive(Clone, Copy)] pub struct S123;
    pub trait Fa123 { fn m(self); }
    impl Fa123 for S123 { fn m(self) { println!("c123 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr123 { fn m(self); }
    impl Tr123 for &S123 { fn m(self) { println!("c123 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &S123) { v.m(); }
    pub fn drive() { probe(&S123); }
}
pub mod c124 {
    #[derive(Clone, Copy)] pub struct S124;
    pub trait Fa124 { fn m(self); }
    impl Fa124 for S124 { fn m(self) { println!("c124 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr124 { fn m(self); }
    impl Tr124 for &mut S124 { fn m(self) { println!("c124 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &S124) { v.m(); }
    pub fn drive() { probe(&S124); }
}
pub mod c125 {
    #[derive(Clone, Copy)] pub struct S125;
    pub trait Fa125 { fn m(self); }
    impl Fa125 for S125 { fn m(self) { println!("c125 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr125 { fn m(self); }
    impl Tr125 for Box<S125> { fn m(self) { println!("c125 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &S125) { v.m(); }
    pub fn drive() { probe(&S125); }
}
pub mod c126 {
    #[derive(Clone, Copy)] pub struct S126;
    pub trait Fa126 { fn m(self); }
    impl Fa126 for S126 { fn m(self) { println!("c126 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr126 { fn m(&self); }
    impl Tr126 for S126 { fn m(&self) { println!("c126 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &S126) { v.m(); }
    pub fn drive() { probe(&S126); }
}
pub mod c127 {
    #[derive(Clone, Copy)] pub struct S127;
    pub trait Fa127 { fn m(self); }
    impl Fa127 for S127 { fn m(self) { println!("c127 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr127 { fn m(&self); }
    impl Tr127 for &S127 { fn m(&self) { println!("c127 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &S127) { v.m(); }
    pub fn drive() { probe(&S127); }
}
pub mod c128 {
    #[derive(Clone, Copy)] pub struct S128;
    pub trait Fa128 { fn m(self); }
    impl Fa128 for S128 { fn m(self) { println!("c128 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr128 { fn m(&self); }
    impl Tr128 for &mut S128 { fn m(&self) { println!("c128 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &S128) { v.m(); }
    pub fn drive() { probe(&S128); }
}
pub mod c129 {
    #[derive(Clone, Copy)] pub struct S129;
    pub trait Fa129 { fn m(self); }
    impl Fa129 for S129 { fn m(self) { println!("c129 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr129 { fn m(&self); }
    impl Tr129 for Box<S129> { fn m(&self) { println!("c129 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &S129) { v.m(); }
    pub fn drive() { probe(&S129); }
}
pub mod c130 {
    #[derive(Clone, Copy)] pub struct S130;
    pub trait Fa130 { fn m(self); }
    impl Fa130 for S130 { fn m(self) { println!("c130 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr130 { fn m(&mut self); }
    impl Tr130 for S130 { fn m(&mut self) { println!("c130 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &S130) { v.m(); }
    pub fn drive() { probe(&S130); }
}
pub mod c131 {
    #[derive(Clone, Copy)] pub struct S131;
    pub trait Fa131 { fn m(self); }
    impl Fa131 for S131 { fn m(self) { println!("c131 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr131 { fn m(&mut self); }
    impl Tr131 for &mut S131 { fn m(&mut self) { println!("c131 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &S131) { v.m(); }
    pub fn drive() { probe(&S131); }
}
pub mod c132 {
    #[derive(Clone, Copy)] pub struct S132;
    pub trait Fa132 { fn m(self); }
    impl Fa132 for S132 { fn m(self) { println!("c132 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr132 { fn m(&mut self); }
    impl Tr132 for Box<S132> { fn m(&mut self) { println!("c132 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &S132) { v.m(); }
    pub fn drive() { probe(&S132); }
}
pub mod c133 {
    #[derive(Clone, Copy)] pub struct S133;
    pub trait Fa133 { fn m(&self); }
    impl Fa133 for S133 { fn m(&self) { println!("c133 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr133 { fn m(self); }
    impl Tr133 for S133 { fn m(self) { println!("c133 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &S133) { v.m(); }
    pub fn drive() { probe(&S133); }
}
pub mod c134 {
    #[derive(Clone, Copy)] pub struct S134;
    pub trait Fa134 { fn m(&self); }
    impl Fa134 for S134 { fn m(&self) { println!("c134 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr134 { fn m(self); }
    impl Tr134 for &mut S134 { fn m(self) { println!("c134 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &S134) { v.m(); }
    pub fn drive() { probe(&S134); }
}
pub mod c135 {
    #[derive(Clone, Copy)] pub struct S135;
    pub trait Fa135 { fn m(&self); }
    impl Fa135 for S135 { fn m(&self) { println!("c135 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr135 { fn m(self); }
    impl Tr135 for Box<S135> { fn m(self) { println!("c135 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &S135) { v.m(); }
    pub fn drive() { probe(&S135); }
}
pub mod c136 {
    #[derive(Clone, Copy)] pub struct S136;
    pub trait Fa136 { fn m(&self); }
    impl Fa136 for S136 { fn m(&self) { println!("c136 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr136 { fn m(&self); }
    impl Tr136 for &S136 { fn m(&self) { println!("c136 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &S136) { v.m(); }
    pub fn drive() { probe(&S136); }
}
pub mod c137 {
    #[derive(Clone, Copy)] pub struct S137;
    pub trait Fa137 { fn m(&self); }
    impl Fa137 for S137 { fn m(&self) { println!("c137 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr137 { fn m(&self); }
    impl Tr137 for &mut S137 { fn m(&self) { println!("c137 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &S137) { v.m(); }
    pub fn drive() { probe(&S137); }
}
pub mod c138 {
    #[derive(Clone, Copy)] pub struct S138;
    pub trait Fa138 { fn m(&self); }
    impl Fa138 for S138 { fn m(&self) { println!("c138 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr138 { fn m(&self); }
    impl Tr138 for Box<S138> { fn m(&self) { println!("c138 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &S138) { v.m(); }
    pub fn drive() { probe(&S138); }
}
pub mod c139 {
    #[derive(Clone, Copy)] pub struct S139;
    pub trait Fa139 { fn m(&self); }
    impl Fa139 for S139 { fn m(&self) { println!("c139 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr139 { fn m(&mut self); }
    impl Tr139 for S139 { fn m(&mut self) { println!("c139 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &S139) { v.m(); }
    pub fn drive() { probe(&S139); }
}
pub mod c140 {
    #[derive(Clone, Copy)] pub struct S140;
    pub trait Fa140 { fn m(&self); }
    impl Fa140 for S140 { fn m(&self) { println!("c140 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr140 { fn m(&mut self); }
    impl Tr140 for &S140 { fn m(&mut self) { println!("c140 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &S140) { v.m(); }
    pub fn drive() { probe(&S140); }
}
pub mod c141 {
    #[derive(Clone, Copy)] pub struct S141;
    pub trait Fa141 { fn m(&self); }
    impl Fa141 for S141 { fn m(&self) { println!("c141 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr141 { fn m(&mut self); }
    impl Tr141 for &mut S141 { fn m(&mut self) { println!("c141 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &S141) { v.m(); }
    pub fn drive() { probe(&S141); }
}
pub mod c142 {
    #[derive(Clone, Copy)] pub struct S142;
    pub trait Fa142 { fn m(&self); }
    impl Fa142 for S142 { fn m(&self) { println!("c142 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr142 { fn m(&mut self); }
    impl Tr142 for Box<S142> { fn m(&mut self) { println!("c142 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &S142) { v.m(); }
    pub fn drive() { probe(&S142); }
}
pub mod c143 {
    #[derive(Clone, Copy)] pub struct S143;
    pub trait Fa143 { fn m(&mut self); }
    impl Fa143 for S143 { fn m(&mut self) { println!("c143 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr143 { fn m(self); }
    impl Tr143 for S143 { fn m(self) { println!("c143 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &S143) { v.m(); }
    pub fn drive() { probe(&S143); }
}
pub mod c144 {
    #[derive(Clone, Copy)] pub struct S144;
    pub trait Fa144 { fn m(&mut self); }
    impl Fa144 for S144 { fn m(&mut self) { println!("c144 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr144 { fn m(self); }
    impl Tr144 for &S144 { fn m(self) { println!("c144 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &S144) { v.m(); }
    pub fn drive() { probe(&S144); }
}
pub mod c145 {
    #[derive(Clone, Copy)] pub struct S145;
    pub trait Fa145 { fn m(&mut self); }
    impl Fa145 for S145 { fn m(&mut self) { println!("c145 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr145 { fn m(&self); }
    impl Tr145 for S145 { fn m(&self) { println!("c145 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &S145) { v.m(); }
    pub fn drive() { probe(&S145); }
}
pub mod c146 {
    #[derive(Clone, Copy)] pub struct S146;
    pub trait Fa146 { fn m(&mut self); }
    impl Fa146 for S146 { fn m(&mut self) { println!("c146 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr146 { fn m(&self); }
    impl Tr146 for &S146 { fn m(&self) { println!("c146 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &S146) { v.m(); }
    pub fn drive() { probe(&S146); }
}
pub mod c147 {
    #[derive(Clone, Copy)] pub struct S147;
    pub trait Fa147 { fn m(self); }
    impl Fa147 for &S147 { fn m(self) { println!("c147 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr147 { fn m(self); }
    impl Tr147 for S147 { fn m(self) { println!("c147 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &S147) { v.m(); }
    pub fn drive() { probe(&S147); }
}
pub mod c148 {
    #[derive(Clone, Copy)] pub struct S148;
    pub trait Fa148 { fn m(self); }
    impl Fa148 for &S148 { fn m(self) { println!("c148 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr148 { fn m(self); }
    impl Tr148 for &mut S148 { fn m(self) { println!("c148 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &S148) { v.m(); }
    pub fn drive() { probe(&S148); }
}
pub mod c149 {
    #[derive(Clone, Copy)] pub struct S149;
    pub trait Fa149 { fn m(self); }
    impl Fa149 for &S149 { fn m(self) { println!("c149 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr149 { fn m(self); }
    impl Tr149 for Box<S149> { fn m(self) { println!("c149 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &S149) { v.m(); }
    pub fn drive() { probe(&S149); }
}
pub mod c150 {
    #[derive(Clone, Copy)] pub struct S150;
    pub trait Fa150 { fn m(self); }
    impl Fa150 for &S150 { fn m(self) { println!("c150 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr150 { fn m(&self); }
    impl Tr150 for &S150 { fn m(&self) { println!("c150 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &S150) { v.m(); }
    pub fn drive() { probe(&S150); }
}
pub mod c151 {
    #[derive(Clone, Copy)] pub struct S151;
    pub trait Fa151 { fn m(self); }
    impl Fa151 for &S151 { fn m(self) { println!("c151 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr151 { fn m(&self); }
    impl Tr151 for &mut S151 { fn m(&self) { println!("c151 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &S151) { v.m(); }
    pub fn drive() { probe(&S151); }
}
pub mod c152 {
    #[derive(Clone, Copy)] pub struct S152;
    pub trait Fa152 { fn m(self); }
    impl Fa152 for &S152 { fn m(self) { println!("c152 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr152 { fn m(&self); }
    impl Tr152 for Box<S152> { fn m(&self) { println!("c152 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &S152) { v.m(); }
    pub fn drive() { probe(&S152); }
}
pub mod c153 {
    #[derive(Clone, Copy)] pub struct S153;
    pub trait Fa153 { fn m(self); }
    impl Fa153 for &S153 { fn m(self) { println!("c153 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr153 { fn m(&mut self); }
    impl Tr153 for S153 { fn m(&mut self) { println!("c153 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &S153) { v.m(); }
    pub fn drive() { probe(&S153); }
}
pub mod c154 {
    #[derive(Clone, Copy)] pub struct S154;
    pub trait Fa154 { fn m(self); }
    impl Fa154 for &S154 { fn m(self) { println!("c154 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr154 { fn m(&mut self); }
    impl Tr154 for &S154 { fn m(&mut self) { println!("c154 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &S154) { v.m(); }
    pub fn drive() { probe(&S154); }
}
pub mod c155 {
    #[derive(Clone, Copy)] pub struct S155;
    pub trait Fa155 { fn m(self); }
    impl Fa155 for &S155 { fn m(self) { println!("c155 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr155 { fn m(&mut self); }
    impl Tr155 for &mut S155 { fn m(&mut self) { println!("c155 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &S155) { v.m(); }
    pub fn drive() { probe(&S155); }
}
pub mod c156 {
    #[derive(Clone, Copy)] pub struct S156;
    pub trait Fa156 { fn m(self); }
    impl Fa156 for &S156 { fn m(self) { println!("c156 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr156 { fn m(&mut self); }
    impl Tr156 for Box<S156> { fn m(&mut self) { println!("c156 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &S156) { v.m(); }
    pub fn drive() { probe(&S156); }
}
pub mod c157 {
    #[derive(Clone, Copy)] pub struct S157;
    pub trait Fa157 { fn m(self); }
    impl Fa157 for &mut S157 { fn m(self) { println!("c157 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr157 { fn m(self); }
    impl Tr157 for S157 { fn m(self) { println!("c157 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &S157) { v.m(); }
    pub fn drive() { probe(&S157); }
}
pub mod c158 {
    #[derive(Clone, Copy)] pub struct S158;
    pub trait Fa158 { fn m(self); }
    impl Fa158 for &mut S158 { fn m(self) { println!("c158 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr158 { fn m(self); }
    impl Tr158 for &S158 { fn m(self) { println!("c158 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &S158) { v.m(); }
    pub fn drive() { probe(&S158); }
}
pub mod c159 {
    #[derive(Clone, Copy)] pub struct S159;
    pub trait Fa159 { fn m(self); }
    impl Fa159 for &mut S159 { fn m(self) { println!("c159 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr159 { fn m(&self); }
    impl Tr159 for S159 { fn m(&self) { println!("c159 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &S159) { v.m(); }
    pub fn drive() { probe(&S159); }
}
pub mod c160 {
    #[derive(Clone, Copy)] pub struct S160;
    pub trait Fa160 { fn m(self); }
    impl Fa160 for &mut S160 { fn m(self) { println!("c160 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr160 { fn m(&self); }
    impl Tr160 for &S160 { fn m(&self) { println!("c160 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &S160) { v.m(); }
    pub fn drive() { probe(&S160); }
}
pub mod c161 {
    #[derive(Clone, Copy)] pub struct S161;
    pub trait Tr161 { fn m(self); }
    impl Tr161 for S161 { fn m(self) { println!("c161 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut S161) { v.m(); }
    pub fn drive() { probe(&mut S161); }
}
pub mod c162 {
    #[derive(Clone, Copy)] pub struct S162;
    pub trait Tr162 { fn m(self); }
    impl Tr162 for &S162 { fn m(self) { println!("c162 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut S162) { v.m(); }
    pub fn drive() { probe(&mut S162); }
}
pub mod c163 {
    #[derive(Clone, Copy)] pub struct S163;
    pub trait Tr163 { fn m(self); }
    impl Tr163 for &mut S163 { fn m(self) { println!("c163 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut S163) { v.m(); }
    pub fn drive() { probe(&mut S163); }
}
pub mod c164 {
    #[derive(Clone, Copy)] pub struct S164;
    pub trait Tr164 { fn m(&self); }
    impl Tr164 for S164 { fn m(&self) { println!("c164 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut S164) { v.m(); }
    pub fn drive() { probe(&mut S164); }
}
pub mod c165 {
    #[derive(Clone, Copy)] pub struct S165;
    pub trait Tr165 { fn m(&self); }
    impl Tr165 for &mut S165 { fn m(&self) { println!("c165 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut S165) { v.m(); }
    pub fn drive() { probe(&mut S165); }
}
pub mod c166 {
    #[derive(Clone, Copy)] pub struct S166;
    pub trait Tr166 { fn m(&mut self); }
    impl Tr166 for S166 { fn m(&mut self) { println!("c166 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut S166) { v.m(); }
    pub fn drive() { probe(&mut S166); }
}
pub mod c167 {
    #[derive(Clone, Copy)] pub struct S167;
    impl S167 { pub fn m(self) { println!("c167 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr167 { fn m(self); }
    impl Tr167 for S167 { fn m(self) { println!("c167 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut S167) { v.m(); }
    pub fn drive() { probe(&mut S167); }
}
pub mod c168 {
    #[derive(Clone, Copy)] pub struct S168;
    impl S168 { pub fn m(self) { println!("c168 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr168 { fn m(self); }
    impl Tr168 for &S168 { fn m(self) { println!("c168 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut S168) { v.m(); }
    pub fn drive() { probe(&mut S168); }
}
pub mod c169 {
    #[derive(Clone, Copy)] pub struct S169;
    impl S169 { pub fn m(self) { println!("c169 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr169 { fn m(self); }
    impl Tr169 for &mut S169 { fn m(self) { println!("c169 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut S169) { v.m(); }
    pub fn drive() { probe(&mut S169); }
}
pub mod c170 {
    #[derive(Clone, Copy)] pub struct S170;
    impl S170 { pub fn m(self) { println!("c170 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr170 { fn m(self); }
    impl Tr170 for Box<S170> { fn m(self) { println!("c170 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut S170) { v.m(); }
    pub fn drive() { probe(&mut S170); }
}
pub mod c171 {
    #[derive(Clone, Copy)] pub struct S171;
    impl S171 { pub fn m(self) { println!("c171 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr171 { fn m(&self); }
    impl Tr171 for S171 { fn m(&self) { println!("c171 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut S171) { v.m(); }
    pub fn drive() { probe(&mut S171); }
}
pub mod c172 {
    #[derive(Clone, Copy)] pub struct S172;
    impl S172 { pub fn m(self) { println!("c172 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr172 { fn m(&self); }
    impl Tr172 for &S172 { fn m(&self) { println!("c172 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut S172) { v.m(); }
    pub fn drive() { probe(&mut S172); }
}
pub mod c173 {
    #[derive(Clone, Copy)] pub struct S173;
    impl S173 { pub fn m(self) { println!("c173 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr173 { fn m(&self); }
    impl Tr173 for &mut S173 { fn m(&self) { println!("c173 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut S173) { v.m(); }
    pub fn drive() { probe(&mut S173); }
}
pub mod c174 {
    #[derive(Clone, Copy)] pub struct S174;
    impl S174 { pub fn m(self) { println!("c174 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr174 { fn m(&self); }
    impl Tr174 for Box<S174> { fn m(&self) { println!("c174 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut S174) { v.m(); }
    pub fn drive() { probe(&mut S174); }
}
pub mod c175 {
    #[derive(Clone, Copy)] pub struct S175;
    impl S175 { pub fn m(self) { println!("c175 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr175 { fn m(&mut self); }
    impl Tr175 for S175 { fn m(&mut self) { println!("c175 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut S175) { v.m(); }
    pub fn drive() { probe(&mut S175); }
}
pub mod c176 {
    #[derive(Clone, Copy)] pub struct S176;
    impl S176 { pub fn m(self) { println!("c176 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr176 { fn m(&mut self); }
    impl Tr176 for &S176 { fn m(&mut self) { println!("c176 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut S176) { v.m(); }
    pub fn drive() { probe(&mut S176); }
}
pub mod c177 {
    #[derive(Clone, Copy)] pub struct S177;
    impl S177 { pub fn m(self) { println!("c177 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr177 { fn m(&mut self); }
    impl Tr177 for Box<S177> { fn m(&mut self) { println!("c177 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut S177) { v.m(); }
    pub fn drive() { probe(&mut S177); }
}
pub mod c178 {
    #[derive(Clone, Copy)] pub struct S178;
    impl S178 { pub fn m(&self) { println!("c178 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr178 { fn m(self); }
    impl Tr178 for S178 { fn m(self) { println!("c178 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut S178) { v.m(); }
    pub fn drive() { probe(&mut S178); }
}
pub mod c179 {
    #[derive(Clone, Copy)] pub struct S179;
    impl S179 { pub fn m(&self) { println!("c179 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr179 { fn m(self); }
    impl Tr179 for &S179 { fn m(self) { println!("c179 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut S179) { v.m(); }
    pub fn drive() { probe(&mut S179); }
}
pub mod c180 {
    #[derive(Clone, Copy)] pub struct S180;
    impl S180 { pub fn m(&self) { println!("c180 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr180 { fn m(self); }
    impl Tr180 for &mut S180 { fn m(self) { println!("c180 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut S180) { v.m(); }
    pub fn drive() { probe(&mut S180); }
}
pub mod c181 {
    #[derive(Clone, Copy)] pub struct S181;
    impl S181 { pub fn m(&self) { println!("c181 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr181 { fn m(self); }
    impl Tr181 for Box<S181> { fn m(self) { println!("c181 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut S181) { v.m(); }
    pub fn drive() { probe(&mut S181); }
}
pub mod c182 {
    #[derive(Clone, Copy)] pub struct S182;
    impl S182 { pub fn m(&self) { println!("c182 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr182 { fn m(&self); }
    impl Tr182 for S182 { fn m(&self) { println!("c182 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut S182) { v.m(); }
    pub fn drive() { probe(&mut S182); }
}
pub mod c183 {
    #[derive(Clone, Copy)] pub struct S183;
    impl S183 { pub fn m(&self) { println!("c183 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr183 { fn m(&self); }
    impl Tr183 for &S183 { fn m(&self) { println!("c183 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut S183) { v.m(); }
    pub fn drive() { probe(&mut S183); }
}
pub mod c184 {
    #[derive(Clone, Copy)] pub struct S184;
    impl S184 { pub fn m(&self) { println!("c184 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr184 { fn m(&self); }
    impl Tr184 for &mut S184 { fn m(&self) { println!("c184 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut S184) { v.m(); }
    pub fn drive() { probe(&mut S184); }
}
pub mod c185 {
    #[derive(Clone, Copy)] pub struct S185;
    impl S185 { pub fn m(&self) { println!("c185 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr185 { fn m(&self); }
    impl Tr185 for Box<S185> { fn m(&self) { println!("c185 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut S185) { v.m(); }
    pub fn drive() { probe(&mut S185); }
}
pub mod c186 {
    #[derive(Clone, Copy)] pub struct S186;
    impl S186 { pub fn m(&self) { println!("c186 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr186 { fn m(&mut self); }
    impl Tr186 for S186 { fn m(&mut self) { println!("c186 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut S186) { v.m(); }
    pub fn drive() { probe(&mut S186); }
}
pub mod c187 {
    #[derive(Clone, Copy)] pub struct S187;
    impl S187 { pub fn m(&self) { println!("c187 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr187 { fn m(&mut self); }
    impl Tr187 for &S187 { fn m(&mut self) { println!("c187 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut S187) { v.m(); }
    pub fn drive() { probe(&mut S187); }
}
pub mod c188 {
    #[derive(Clone, Copy)] pub struct S188;
    impl S188 { pub fn m(&self) { println!("c188 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr188 { fn m(&mut self); }
    impl Tr188 for Box<S188> { fn m(&mut self) { println!("c188 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut S188) { v.m(); }
    pub fn drive() { probe(&mut S188); }
}
pub mod c189 {
    #[derive(Clone, Copy)] pub struct S189;
    impl S189 { pub fn m(&mut self) { println!("c189 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr189 { fn m(self); }
    impl Tr189 for S189 { fn m(self) { println!("c189 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut S189) { v.m(); }
    pub fn drive() { probe(&mut S189); }
}
pub mod c190 {
    #[derive(Clone, Copy)] pub struct S190;
    impl S190 { pub fn m(&mut self) { println!("c190 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr190 { fn m(self); }
    impl Tr190 for &S190 { fn m(self) { println!("c190 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut S190) { v.m(); }
    pub fn drive() { probe(&mut S190); }
}
pub mod c191 {
    #[derive(Clone, Copy)] pub struct S191;
    impl S191 { pub fn m(&mut self) { println!("c191 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr191 { fn m(self); }
    impl Tr191 for &mut S191 { fn m(self) { println!("c191 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut S191) { v.m(); }
    pub fn drive() { probe(&mut S191); }
}
pub mod c192 {
    #[derive(Clone, Copy)] pub struct S192;
    impl S192 { pub fn m(&mut self) { println!("c192 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr192 { fn m(self); }
    impl Tr192 for Box<S192> { fn m(self) { println!("c192 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut S192) { v.m(); }
    pub fn drive() { probe(&mut S192); }
}
pub mod c193 {
    #[derive(Clone, Copy)] pub struct S193;
    impl S193 { pub fn m(&mut self) { println!("c193 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr193 { fn m(&self); }
    impl Tr193 for S193 { fn m(&self) { println!("c193 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut S193) { v.m(); }
    pub fn drive() { probe(&mut S193); }
}
pub mod c194 {
    #[derive(Clone, Copy)] pub struct S194;
    impl S194 { pub fn m(&mut self) { println!("c194 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr194 { fn m(&self); }
    impl Tr194 for &S194 { fn m(&self) { println!("c194 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut S194) { v.m(); }
    pub fn drive() { probe(&mut S194); }
}
pub mod c195 {
    #[derive(Clone, Copy)] pub struct S195;
    impl S195 { pub fn m(&mut self) { println!("c195 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr195 { fn m(&self); }
    impl Tr195 for &mut S195 { fn m(&self) { println!("c195 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut S195) { v.m(); }
    pub fn drive() { probe(&mut S195); }
}
pub mod c196 {
    #[derive(Clone, Copy)] pub struct S196;
    impl S196 { pub fn m(&mut self) { println!("c196 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr196 { fn m(&self); }
    impl Tr196 for Box<S196> { fn m(&self) { println!("c196 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut S196) { v.m(); }
    pub fn drive() { probe(&mut S196); }
}
pub mod c197 {
    #[derive(Clone, Copy)] pub struct S197;
    impl S197 { pub fn m(&mut self) { println!("c197 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr197 { fn m(&mut self); }
    impl Tr197 for S197 { fn m(&mut self) { println!("c197 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut S197) { v.m(); }
    pub fn drive() { probe(&mut S197); }
}
pub mod c198 {
    #[derive(Clone, Copy)] pub struct S198;
    impl S198 { pub fn m(&mut self) { println!("c198 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr198 { fn m(&mut self); }
    impl Tr198 for &S198 { fn m(&mut self) { println!("c198 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut S198) { v.m(); }
    pub fn drive() { probe(&mut S198); }
}
pub mod c199 {
    #[derive(Clone, Copy)] pub struct S199;
    impl S199 { pub fn m(&mut self) { println!("c199 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr199 { fn m(&mut self); }
    impl Tr199 for &mut S199 { fn m(&mut self) { println!("c199 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut S199) { v.m(); }
    pub fn drive() { probe(&mut S199); }
}
pub mod c200 {
    #[derive(Clone, Copy)] pub struct S200;
    impl S200 { pub fn m(&mut self) { println!("c200 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr200 { fn m(&mut self); }
    impl Tr200 for Box<S200> { fn m(&mut self) { println!("c200 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut S200) { v.m(); }
    pub fn drive() { probe(&mut S200); }
}
pub mod c201 {
    #[derive(Clone, Copy)] pub struct S201;
    pub trait Fa201 { fn m(self); }
    impl Fa201 for S201 { fn m(self) { println!("c201 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr201 { fn m(self); }
    impl Tr201 for &S201 { fn m(self) { println!("c201 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut S201) { v.m(); }
    pub fn drive() { probe(&mut S201); }
}
pub mod c202 {
    #[derive(Clone, Copy)] pub struct S202;
    pub trait Fa202 { fn m(self); }
    impl Fa202 for S202 { fn m(self) { println!("c202 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr202 { fn m(self); }
    impl Tr202 for &mut S202 { fn m(self) { println!("c202 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut S202) { v.m(); }
    pub fn drive() { probe(&mut S202); }
}
pub mod c203 {
    #[derive(Clone, Copy)] pub struct S203;
    pub trait Fa203 { fn m(self); }
    impl Fa203 for S203 { fn m(self) { println!("c203 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr203 { fn m(self); }
    impl Tr203 for Box<S203> { fn m(self) { println!("c203 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut S203) { v.m(); }
    pub fn drive() { probe(&mut S203); }
}
pub mod c204 {
    #[derive(Clone, Copy)] pub struct S204;
    pub trait Fa204 { fn m(self); }
    impl Fa204 for S204 { fn m(self) { println!("c204 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr204 { fn m(&self); }
    impl Tr204 for S204 { fn m(&self) { println!("c204 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut S204) { v.m(); }
    pub fn drive() { probe(&mut S204); }
}
pub mod c205 {
    #[derive(Clone, Copy)] pub struct S205;
    pub trait Fa205 { fn m(self); }
    impl Fa205 for S205 { fn m(self) { println!("c205 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr205 { fn m(&self); }
    impl Tr205 for &S205 { fn m(&self) { println!("c205 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut S205) { v.m(); }
    pub fn drive() { probe(&mut S205); }
}
pub mod c206 {
    #[derive(Clone, Copy)] pub struct S206;
    pub trait Fa206 { fn m(self); }
    impl Fa206 for S206 { fn m(self) { println!("c206 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr206 { fn m(&self); }
    impl Tr206 for &mut S206 { fn m(&self) { println!("c206 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut S206) { v.m(); }
    pub fn drive() { probe(&mut S206); }
}
pub mod c207 {
    #[derive(Clone, Copy)] pub struct S207;
    pub trait Fa207 { fn m(self); }
    impl Fa207 for S207 { fn m(self) { println!("c207 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr207 { fn m(&self); }
    impl Tr207 for Box<S207> { fn m(&self) { println!("c207 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut S207) { v.m(); }
    pub fn drive() { probe(&mut S207); }
}
pub mod c208 {
    #[derive(Clone, Copy)] pub struct S208;
    pub trait Fa208 { fn m(self); }
    impl Fa208 for S208 { fn m(self) { println!("c208 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr208 { fn m(&mut self); }
    impl Tr208 for S208 { fn m(&mut self) { println!("c208 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut S208) { v.m(); }
    pub fn drive() { probe(&mut S208); }
}
pub mod c209 {
    #[derive(Clone, Copy)] pub struct S209;
    pub trait Fa209 { fn m(self); }
    impl Fa209 for S209 { fn m(self) { println!("c209 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr209 { fn m(&mut self); }
    impl Tr209 for &S209 { fn m(&mut self) { println!("c209 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut S209) { v.m(); }
    pub fn drive() { probe(&mut S209); }
}
pub mod c210 {
    #[derive(Clone, Copy)] pub struct S210;
    pub trait Fa210 { fn m(self); }
    impl Fa210 for S210 { fn m(self) { println!("c210 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr210 { fn m(&mut self); }
    impl Tr210 for Box<S210> { fn m(&mut self) { println!("c210 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut S210) { v.m(); }
    pub fn drive() { probe(&mut S210); }
}
pub mod c211 {
    #[derive(Clone, Copy)] pub struct S211;
    pub trait Fa211 { fn m(&self); }
    impl Fa211 for S211 { fn m(&self) { println!("c211 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr211 { fn m(self); }
    impl Tr211 for S211 { fn m(self) { println!("c211 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut S211) { v.m(); }
    pub fn drive() { probe(&mut S211); }
}
pub mod c212 {
    #[derive(Clone, Copy)] pub struct S212;
    pub trait Fa212 { fn m(&self); }
    impl Fa212 for S212 { fn m(&self) { println!("c212 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr212 { fn m(self); }
    impl Tr212 for &mut S212 { fn m(self) { println!("c212 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut S212) { v.m(); }
    pub fn drive() { probe(&mut S212); }
}
pub mod c213 {
    #[derive(Clone, Copy)] pub struct S213;
    pub trait Fa213 { fn m(&self); }
    impl Fa213 for S213 { fn m(&self) { println!("c213 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr213 { fn m(self); }
    impl Tr213 for Box<S213> { fn m(self) { println!("c213 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut S213) { v.m(); }
    pub fn drive() { probe(&mut S213); }
}
pub mod c214 {
    #[derive(Clone, Copy)] pub struct S214;
    pub trait Fa214 { fn m(&self); }
    impl Fa214 for S214 { fn m(&self) { println!("c214 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr214 { fn m(&self); }
    impl Tr214 for &S214 { fn m(&self) { println!("c214 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut S214) { v.m(); }
    pub fn drive() { probe(&mut S214); }
}
pub mod c215 {
    #[derive(Clone, Copy)] pub struct S215;
    pub trait Fa215 { fn m(&self); }
    impl Fa215 for S215 { fn m(&self) { println!("c215 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr215 { fn m(&self); }
    impl Tr215 for &mut S215 { fn m(&self) { println!("c215 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut S215) { v.m(); }
    pub fn drive() { probe(&mut S215); }
}
pub mod c216 {
    #[derive(Clone, Copy)] pub struct S216;
    pub trait Fa216 { fn m(&self); }
    impl Fa216 for S216 { fn m(&self) { println!("c216 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr216 { fn m(&self); }
    impl Tr216 for Box<S216> { fn m(&self) { println!("c216 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut S216) { v.m(); }
    pub fn drive() { probe(&mut S216); }
}
pub mod c217 {
    #[derive(Clone, Copy)] pub struct S217;
    pub trait Fa217 { fn m(&self); }
    impl Fa217 for S217 { fn m(&self) { println!("c217 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr217 { fn m(&mut self); }
    impl Tr217 for S217 { fn m(&mut self) { println!("c217 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut S217) { v.m(); }
    pub fn drive() { probe(&mut S217); }
}
pub mod c218 {
    #[derive(Clone, Copy)] pub struct S218;
    pub trait Fa218 { fn m(&self); }
    impl Fa218 for S218 { fn m(&self) { println!("c218 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr218 { fn m(&mut self); }
    impl Tr218 for &S218 { fn m(&mut self) { println!("c218 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut S218) { v.m(); }
    pub fn drive() { probe(&mut S218); }
}
pub mod c219 {
    #[derive(Clone, Copy)] pub struct S219;
    pub trait Fa219 { fn m(&self); }
    impl Fa219 for S219 { fn m(&self) { println!("c219 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr219 { fn m(&mut self); }
    impl Tr219 for Box<S219> { fn m(&mut self) { println!("c219 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut S219) { v.m(); }
    pub fn drive() { probe(&mut S219); }
}
pub mod c220 {
    #[derive(Clone, Copy)] pub struct S220;
    pub trait Fa220 { fn m(&mut self); }
    impl Fa220 for S220 { fn m(&mut self) { println!("c220 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr220 { fn m(self); }
    impl Tr220 for S220 { fn m(self) { println!("c220 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut S220) { v.m(); }
    pub fn drive() { probe(&mut S220); }
}
pub mod c221 {
    #[derive(Clone, Copy)] pub struct S221;
    pub trait Fa221 { fn m(&mut self); }
    impl Fa221 for S221 { fn m(&mut self) { println!("c221 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr221 { fn m(self); }
    impl Tr221 for &S221 { fn m(self) { println!("c221 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut S221) { v.m(); }
    pub fn drive() { probe(&mut S221); }
}
pub mod c222 {
    #[derive(Clone, Copy)] pub struct S222;
    pub trait Fa222 { fn m(&mut self); }
    impl Fa222 for S222 { fn m(&mut self) { println!("c222 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr222 { fn m(self); }
    impl Tr222 for Box<S222> { fn m(self) { println!("c222 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut S222) { v.m(); }
    pub fn drive() { probe(&mut S222); }
}
pub mod c223 {
    #[derive(Clone, Copy)] pub struct S223;
    pub trait Fa223 { fn m(&mut self); }
    impl Fa223 for S223 { fn m(&mut self) { println!("c223 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr223 { fn m(&self); }
    impl Tr223 for S223 { fn m(&self) { println!("c223 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut S223) { v.m(); }
    pub fn drive() { probe(&mut S223); }
}
pub mod c224 {
    #[derive(Clone, Copy)] pub struct S224;
    pub trait Fa224 { fn m(&mut self); }
    impl Fa224 for S224 { fn m(&mut self) { println!("c224 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr224 { fn m(&self); }
    impl Tr224 for &S224 { fn m(&self) { println!("c224 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut S224) { v.m(); }
    pub fn drive() { probe(&mut S224); }
}
pub mod c225 {
    #[derive(Clone, Copy)] pub struct S225;
    pub trait Fa225 { fn m(&mut self); }
    impl Fa225 for S225 { fn m(&mut self) { println!("c225 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr225 { fn m(&self); }
    impl Tr225 for &mut S225 { fn m(&self) { println!("c225 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut S225) { v.m(); }
    pub fn drive() { probe(&mut S225); }
}
pub mod c226 {
    #[derive(Clone, Copy)] pub struct S226;
    pub trait Fa226 { fn m(&mut self); }
    impl Fa226 for S226 { fn m(&mut self) { println!("c226 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr226 { fn m(&self); }
    impl Tr226 for Box<S226> { fn m(&self) { println!("c226 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut S226) { v.m(); }
    pub fn drive() { probe(&mut S226); }
}
pub mod c227 {
    #[derive(Clone, Copy)] pub struct S227;
    pub trait Fa227 { fn m(&mut self); }
    impl Fa227 for S227 { fn m(&mut self) { println!("c227 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr227 { fn m(&mut self); }
    impl Tr227 for &S227 { fn m(&mut self) { println!("c227 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut S227) { v.m(); }
    pub fn drive() { probe(&mut S227); }
}
pub mod c228 {
    #[derive(Clone, Copy)] pub struct S228;
    pub trait Fa228 { fn m(&mut self); }
    impl Fa228 for S228 { fn m(&mut self) { println!("c228 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr228 { fn m(&mut self); }
    impl Tr228 for &mut S228 { fn m(&mut self) { println!("c228 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut S228) { v.m(); }
    pub fn drive() { probe(&mut S228); }
}
pub mod c229 {
    #[derive(Clone, Copy)] pub struct S229;
    pub trait Fa229 { fn m(&mut self); }
    impl Fa229 for S229 { fn m(&mut self) { println!("c229 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr229 { fn m(&mut self); }
    impl Tr229 for Box<S229> { fn m(&mut self) { println!("c229 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut S229) { v.m(); }
    pub fn drive() { probe(&mut S229); }
}
pub mod c230 {
    #[derive(Clone, Copy)] pub struct S230;
    pub trait Fa230 { fn m(self); }
    impl Fa230 for &S230 { fn m(self) { println!("c230 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr230 { fn m(self); }
    impl Tr230 for S230 { fn m(self) { println!("c230 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut S230) { v.m(); }
    pub fn drive() { probe(&mut S230); }
}
pub mod c231 {
    #[derive(Clone, Copy)] pub struct S231;
    pub trait Fa231 { fn m(self); }
    impl Fa231 for &S231 { fn m(self) { println!("c231 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr231 { fn m(self); }
    impl Tr231 for &mut S231 { fn m(self) { println!("c231 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut S231) { v.m(); }
    pub fn drive() { probe(&mut S231); }
}
pub mod c232 {
    #[derive(Clone, Copy)] pub struct S232;
    pub trait Fa232 { fn m(self); }
    impl Fa232 for &S232 { fn m(self) { println!("c232 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr232 { fn m(self); }
    impl Tr232 for Box<S232> { fn m(self) { println!("c232 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut S232) { v.m(); }
    pub fn drive() { probe(&mut S232); }
}
pub mod c233 {
    #[derive(Clone, Copy)] pub struct S233;
    pub trait Fa233 { fn m(self); }
    impl Fa233 for &S233 { fn m(self) { println!("c233 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr233 { fn m(&self); }
    impl Tr233 for &S233 { fn m(&self) { println!("c233 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut S233) { v.m(); }
    pub fn drive() { probe(&mut S233); }
}
pub mod c234 {
    #[derive(Clone, Copy)] pub struct S234;
    pub trait Fa234 { fn m(self); }
    impl Fa234 for &S234 { fn m(self) { println!("c234 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr234 { fn m(&self); }
    impl Tr234 for &mut S234 { fn m(&self) { println!("c234 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut S234) { v.m(); }
    pub fn drive() { probe(&mut S234); }
}
pub mod c235 {
    #[derive(Clone, Copy)] pub struct S235;
    pub trait Fa235 { fn m(self); }
    impl Fa235 for &S235 { fn m(self) { println!("c235 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr235 { fn m(&self); }
    impl Tr235 for Box<S235> { fn m(&self) { println!("c235 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut S235) { v.m(); }
    pub fn drive() { probe(&mut S235); }
}
pub mod c236 {
    #[derive(Clone, Copy)] pub struct S236;
    pub trait Fa236 { fn m(self); }
    impl Fa236 for &S236 { fn m(self) { println!("c236 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr236 { fn m(&mut self); }
    impl Tr236 for S236 { fn m(&mut self) { println!("c236 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut S236) { v.m(); }
    pub fn drive() { probe(&mut S236); }
}
pub mod c237 {
    #[derive(Clone, Copy)] pub struct S237;
    pub trait Fa237 { fn m(self); }
    impl Fa237 for &S237 { fn m(self) { println!("c237 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr237 { fn m(&mut self); }
    impl Tr237 for &S237 { fn m(&mut self) { println!("c237 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut S237) { v.m(); }
    pub fn drive() { probe(&mut S237); }
}
pub mod c238 {
    #[derive(Clone, Copy)] pub struct S238;
    pub trait Fa238 { fn m(self); }
    impl Fa238 for &S238 { fn m(self) { println!("c238 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr238 { fn m(&mut self); }
    impl Tr238 for Box<S238> { fn m(&mut self) { println!("c238 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut S238) { v.m(); }
    pub fn drive() { probe(&mut S238); }
}
pub mod c239 {
    #[derive(Clone, Copy)] pub struct S239;
    pub trait Fa239 { fn m(self); }
    impl Fa239 for &mut S239 { fn m(self) { println!("c239 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr239 { fn m(self); }
    impl Tr239 for S239 { fn m(self) { println!("c239 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut S239) { v.m(); }
    pub fn drive() { probe(&mut S239); }
}
pub mod c240 {
    #[derive(Clone, Copy)] pub struct S240;
    pub trait Fa240 { fn m(self); }
    impl Fa240 for &mut S240 { fn m(self) { println!("c240 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr240 { fn m(self); }
    impl Tr240 for &S240 { fn m(self) { println!("c240 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut S240) { v.m(); }
    pub fn drive() { probe(&mut S240); }
}
pub mod c241 {
    #[derive(Clone, Copy)] pub struct S241;
    pub trait Fa241 { fn m(self); }
    impl Fa241 for &mut S241 { fn m(self) { println!("c241 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr241 { fn m(self); }
    impl Tr241 for Box<S241> { fn m(self) { println!("c241 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut S241) { v.m(); }
    pub fn drive() { probe(&mut S241); }
}
pub mod c242 {
    #[derive(Clone, Copy)] pub struct S242;
    pub trait Fa242 { fn m(self); }
    impl Fa242 for &mut S242 { fn m(self) { println!("c242 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr242 { fn m(&self); }
    impl Tr242 for S242 { fn m(&self) { println!("c242 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut S242) { v.m(); }
    pub fn drive() { probe(&mut S242); }
}
pub mod c243 {
    #[derive(Clone, Copy)] pub struct S243;
    pub trait Fa243 { fn m(self); }
    impl Fa243 for &mut S243 { fn m(self) { println!("c243 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr243 { fn m(&self); }
    impl Tr243 for &S243 { fn m(&self) { println!("c243 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut S243) { v.m(); }
    pub fn drive() { probe(&mut S243); }
}
pub mod c244 {
    #[derive(Clone, Copy)] pub struct S244;
    pub trait Fa244 { fn m(self); }
    impl Fa244 for &mut S244 { fn m(self) { println!("c244 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr244 { fn m(&self); }
    impl Tr244 for &mut S244 { fn m(&self) { println!("c244 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut S244) { v.m(); }
    pub fn drive() { probe(&mut S244); }
}
pub mod c245 {
    #[derive(Clone, Copy)] pub struct S245;
    pub trait Fa245 { fn m(self); }
    impl Fa245 for &mut S245 { fn m(self) { println!("c245 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr245 { fn m(&self); }
    impl Tr245 for Box<S245> { fn m(&self) { println!("c245 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut S245) { v.m(); }
    pub fn drive() { probe(&mut S245); }
}
pub mod c246 {
    #[derive(Clone, Copy)] pub struct S246;
    pub trait Fa246 { fn m(self); }
    impl Fa246 for &mut S246 { fn m(self) { println!("c246 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr246 { fn m(&mut self); }
    impl Tr246 for &S246 { fn m(&mut self) { println!("c246 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut S246) { v.m(); }
    pub fn drive() { probe(&mut S246); }
}
pub mod c247 {
    #[derive(Clone, Copy)] pub struct S247;
    pub trait Fa247 { fn m(self); }
    impl Fa247 for &mut S247 { fn m(self) { println!("c247 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr247 { fn m(&mut self); }
    impl Tr247 for &mut S247 { fn m(&mut self) { println!("c247 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut S247) { v.m(); }
    pub fn drive() { probe(&mut S247); }
}
pub mod c248 {
    #[derive(Clone, Copy)] pub struct S248;
    pub trait Fa248 { fn m(self); }
    impl Fa248 for &mut S248 { fn m(self) { println!("c248 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr248 { fn m(&mut self); }
    impl Tr248 for Box<S248> { fn m(&mut self) { println!("c248 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut S248) { v.m(); }
    pub fn drive() { probe(&mut S248); }
}
pub mod c249 {
    #[derive(Clone, Copy)] pub struct S249;
    pub trait Tr249 { fn m(self); }
    impl Tr249 for S249 { fn m(self) { println!("c249 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(mut v: Box<S249>) { v.m(); }
    pub fn drive() { probe(Box::new(S249)); }
}
pub mod c250 {
    #[derive(Clone, Copy)] pub struct S250;
    pub trait Tr250 { fn m(self); }
    impl Tr250 for &S250 { fn m(self) { println!("c250 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(mut v: Box<S250>) { v.m(); }
    pub fn drive() { probe(Box::new(S250)); }
}
pub mod c251 {
    #[derive(Clone, Copy)] pub struct S251;
    pub trait Tr251 { fn m(self); }
    impl Tr251 for &mut S251 { fn m(self) { println!("c251 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(mut v: Box<S251>) { v.m(); }
    pub fn drive() { probe(Box::new(S251)); }
}
pub mod c252 {
    #[derive(Clone, Copy)] pub struct S252;
    pub trait Tr252 { fn m(self); }
    impl Tr252 for Box<S252> { fn m(self) { println!("c252 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(mut v: Box<S252>) { v.m(); }
    pub fn drive() { probe(Box::new(S252)); }
}
pub mod c253 {
    #[derive(Clone, Copy)] pub struct S253;
    pub trait Tr253 { fn m(&self); }
    impl Tr253 for S253 { fn m(&self) { println!("c253 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(mut v: Box<S253>) { v.m(); }
    pub fn drive() { probe(Box::new(S253)); }
}
pub mod c254 {
    #[derive(Clone, Copy)] pub struct S254;
    pub trait Tr254 { fn m(&self); }
    impl Tr254 for Box<S254> { fn m(&self) { println!("c254 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(mut v: Box<S254>) { v.m(); }
    pub fn drive() { probe(Box::new(S254)); }
}
pub mod c255 {
    #[derive(Clone, Copy)] pub struct S255;
    pub trait Tr255 { fn m(&mut self); }
    impl Tr255 for S255 { fn m(&mut self) { println!("c255 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(mut v: Box<S255>) { v.m(); }
    pub fn drive() { probe(Box::new(S255)); }
}
pub mod c256 {
    #[derive(Clone, Copy)] pub struct S256;
    pub trait Tr256 { fn m(&mut self); }
    impl Tr256 for Box<S256> { fn m(&mut self) { println!("c256 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(mut v: Box<S256>) { v.m(); }
    pub fn drive() { probe(Box::new(S256)); }
}
pub mod c257 {
    #[derive(Clone, Copy)] pub struct S257;
    impl S257 { pub fn m(self) { println!("c257 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr257 { fn m(self); }
    impl Tr257 for S257 { fn m(self) { println!("c257 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(mut v: Box<S257>) { v.m(); }
    pub fn drive() { probe(Box::new(S257)); }
}
pub mod c258 {
    #[derive(Clone, Copy)] pub struct S258;
    impl S258 { pub fn m(self) { println!("c258 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr258 { fn m(self); }
    impl Tr258 for &S258 { fn m(self) { println!("c258 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(mut v: Box<S258>) { v.m(); }
    pub fn drive() { probe(Box::new(S258)); }
}
pub mod c259 {
    #[derive(Clone, Copy)] pub struct S259;
    impl S259 { pub fn m(self) { println!("c259 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr259 { fn m(self); }
    impl Tr259 for &mut S259 { fn m(self) { println!("c259 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(mut v: Box<S259>) { v.m(); }
    pub fn drive() { probe(Box::new(S259)); }
}
pub mod c260 {
    #[derive(Clone, Copy)] pub struct S260;
    impl S260 { pub fn m(self) { println!("c260 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr260 { fn m(self); }
    impl Tr260 for Box<S260> { fn m(self) { println!("c260 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(mut v: Box<S260>) { v.m(); }
    pub fn drive() { probe(Box::new(S260)); }
}
pub mod c261 {
    #[derive(Clone, Copy)] pub struct S261;
    impl S261 { pub fn m(self) { println!("c261 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr261 { fn m(&self); }
    impl Tr261 for S261 { fn m(&self) { println!("c261 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(mut v: Box<S261>) { v.m(); }
    pub fn drive() { probe(Box::new(S261)); }
}
pub mod c262 {
    #[derive(Clone, Copy)] pub struct S262;
    impl S262 { pub fn m(self) { println!("c262 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr262 { fn m(&self); }
    impl Tr262 for &S262 { fn m(&self) { println!("c262 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(mut v: Box<S262>) { v.m(); }
    pub fn drive() { probe(Box::new(S262)); }
}
pub mod c263 {
    #[derive(Clone, Copy)] pub struct S263;
    impl S263 { pub fn m(self) { println!("c263 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr263 { fn m(&self); }
    impl Tr263 for &mut S263 { fn m(&self) { println!("c263 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(mut v: Box<S263>) { v.m(); }
    pub fn drive() { probe(Box::new(S263)); }
}
pub mod c264 {
    #[derive(Clone, Copy)] pub struct S264;
    impl S264 { pub fn m(self) { println!("c264 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr264 { fn m(&self); }
    impl Tr264 for Box<S264> { fn m(&self) { println!("c264 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(mut v: Box<S264>) { v.m(); }
    pub fn drive() { probe(Box::new(S264)); }
}
pub mod c265 {
    #[derive(Clone, Copy)] pub struct S265;
    impl S265 { pub fn m(self) { println!("c265 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr265 { fn m(&mut self); }
    impl Tr265 for S265 { fn m(&mut self) { println!("c265 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(mut v: Box<S265>) { v.m(); }
    pub fn drive() { probe(Box::new(S265)); }
}
pub mod c266 {
    #[derive(Clone, Copy)] pub struct S266;
    impl S266 { pub fn m(self) { println!("c266 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr266 { fn m(&mut self); }
    impl Tr266 for &S266 { fn m(&mut self) { println!("c266 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(mut v: Box<S266>) { v.m(); }
    pub fn drive() { probe(Box::new(S266)); }
}
pub mod c267 {
    #[derive(Clone, Copy)] pub struct S267;
    impl S267 { pub fn m(self) { println!("c267 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr267 { fn m(&mut self); }
    impl Tr267 for &mut S267 { fn m(&mut self) { println!("c267 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(mut v: Box<S267>) { v.m(); }
    pub fn drive() { probe(Box::new(S267)); }
}
pub mod c268 {
    #[derive(Clone, Copy)] pub struct S268;
    impl S268 { pub fn m(self) { println!("c268 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr268 { fn m(&mut self); }
    impl Tr268 for Box<S268> { fn m(&mut self) { println!("c268 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(mut v: Box<S268>) { v.m(); }
    pub fn drive() { probe(Box::new(S268)); }
}
pub mod c269 {
    #[derive(Clone, Copy)] pub struct S269;
    impl S269 { pub fn m(&self) { println!("c269 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr269 { fn m(self); }
    impl Tr269 for S269 { fn m(self) { println!("c269 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(mut v: Box<S269>) { v.m(); }
    pub fn drive() { probe(Box::new(S269)); }
}
pub mod c270 {
    #[derive(Clone, Copy)] pub struct S270;
    impl S270 { pub fn m(&self) { println!("c270 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr270 { fn m(self); }
    impl Tr270 for &S270 { fn m(self) { println!("c270 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(mut v: Box<S270>) { v.m(); }
    pub fn drive() { probe(Box::new(S270)); }
}
pub mod c271 {
    #[derive(Clone, Copy)] pub struct S271;
    impl S271 { pub fn m(&self) { println!("c271 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr271 { fn m(self); }
    impl Tr271 for &mut S271 { fn m(self) { println!("c271 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(mut v: Box<S271>) { v.m(); }
    pub fn drive() { probe(Box::new(S271)); }
}
pub mod c272 {
    #[derive(Clone, Copy)] pub struct S272;
    impl S272 { pub fn m(&self) { println!("c272 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr272 { fn m(self); }
    impl Tr272 for Box<S272> { fn m(self) { println!("c272 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(mut v: Box<S272>) { v.m(); }
    pub fn drive() { probe(Box::new(S272)); }
}
pub mod c273 {
    #[derive(Clone, Copy)] pub struct S273;
    impl S273 { pub fn m(&self) { println!("c273 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr273 { fn m(&self); }
    impl Tr273 for S273 { fn m(&self) { println!("c273 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(mut v: Box<S273>) { v.m(); }
    pub fn drive() { probe(Box::new(S273)); }
}
pub mod c274 {
    #[derive(Clone, Copy)] pub struct S274;
    impl S274 { pub fn m(&self) { println!("c274 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr274 { fn m(&self); }
    impl Tr274 for &S274 { fn m(&self) { println!("c274 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(mut v: Box<S274>) { v.m(); }
    pub fn drive() { probe(Box::new(S274)); }
}
pub mod c275 {
    #[derive(Clone, Copy)] pub struct S275;
    impl S275 { pub fn m(&self) { println!("c275 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr275 { fn m(&self); }
    impl Tr275 for &mut S275 { fn m(&self) { println!("c275 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(mut v: Box<S275>) { v.m(); }
    pub fn drive() { probe(Box::new(S275)); }
}
pub mod c276 {
    #[derive(Clone, Copy)] pub struct S276;
    impl S276 { pub fn m(&self) { println!("c276 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr276 { fn m(&self); }
    impl Tr276 for Box<S276> { fn m(&self) { println!("c276 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(mut v: Box<S276>) { v.m(); }
    pub fn drive() { probe(Box::new(S276)); }
}
pub mod c277 {
    #[derive(Clone, Copy)] pub struct S277;
    impl S277 { pub fn m(&self) { println!("c277 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr277 { fn m(&mut self); }
    impl Tr277 for S277 { fn m(&mut self) { println!("c277 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(mut v: Box<S277>) { v.m(); }
    pub fn drive() { probe(Box::new(S277)); }
}
pub mod c278 {
    #[derive(Clone, Copy)] pub struct S278;
    impl S278 { pub fn m(&self) { println!("c278 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr278 { fn m(&mut self); }
    impl Tr278 for &S278 { fn m(&mut self) { println!("c278 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(mut v: Box<S278>) { v.m(); }
    pub fn drive() { probe(Box::new(S278)); }
}
pub mod c279 {
    #[derive(Clone, Copy)] pub struct S279;
    impl S279 { pub fn m(&self) { println!("c279 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr279 { fn m(&mut self); }
    impl Tr279 for &mut S279 { fn m(&mut self) { println!("c279 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(mut v: Box<S279>) { v.m(); }
    pub fn drive() { probe(Box::new(S279)); }
}
pub mod c280 {
    #[derive(Clone, Copy)] pub struct S280;
    impl S280 { pub fn m(&self) { println!("c280 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr280 { fn m(&mut self); }
    impl Tr280 for Box<S280> { fn m(&mut self) { println!("c280 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(mut v: Box<S280>) { v.m(); }
    pub fn drive() { probe(Box::new(S280)); }
}
pub mod c281 {
    #[derive(Clone, Copy)] pub struct S281;
    impl S281 { pub fn m(&mut self) { println!("c281 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr281 { fn m(self); }
    impl Tr281 for S281 { fn m(self) { println!("c281 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(mut v: Box<S281>) { v.m(); }
    pub fn drive() { probe(Box::new(S281)); }
}
pub mod c282 {
    #[derive(Clone, Copy)] pub struct S282;
    impl S282 { pub fn m(&mut self) { println!("c282 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr282 { fn m(self); }
    impl Tr282 for &S282 { fn m(self) { println!("c282 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(mut v: Box<S282>) { v.m(); }
    pub fn drive() { probe(Box::new(S282)); }
}
pub mod c283 {
    #[derive(Clone, Copy)] pub struct S283;
    impl S283 { pub fn m(&mut self) { println!("c283 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr283 { fn m(self); }
    impl Tr283 for &mut S283 { fn m(self) { println!("c283 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(mut v: Box<S283>) { v.m(); }
    pub fn drive() { probe(Box::new(S283)); }
}
pub mod c284 {
    #[derive(Clone, Copy)] pub struct S284;
    impl S284 { pub fn m(&mut self) { println!("c284 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr284 { fn m(self); }
    impl Tr284 for Box<S284> { fn m(self) { println!("c284 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(mut v: Box<S284>) { v.m(); }
    pub fn drive() { probe(Box::new(S284)); }
}
pub mod c285 {
    #[derive(Clone, Copy)] pub struct S285;
    impl S285 { pub fn m(&mut self) { println!("c285 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr285 { fn m(&self); }
    impl Tr285 for S285 { fn m(&self) { println!("c285 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(mut v: Box<S285>) { v.m(); }
    pub fn drive() { probe(Box::new(S285)); }
}
pub mod c286 {
    #[derive(Clone, Copy)] pub struct S286;
    impl S286 { pub fn m(&mut self) { println!("c286 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr286 { fn m(&self); }
    impl Tr286 for &S286 { fn m(&self) { println!("c286 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(mut v: Box<S286>) { v.m(); }
    pub fn drive() { probe(Box::new(S286)); }
}
pub mod c287 {
    #[derive(Clone, Copy)] pub struct S287;
    impl S287 { pub fn m(&mut self) { println!("c287 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr287 { fn m(&self); }
    impl Tr287 for &mut S287 { fn m(&self) { println!("c287 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(mut v: Box<S287>) { v.m(); }
    pub fn drive() { probe(Box::new(S287)); }
}
pub mod c288 {
    #[derive(Clone, Copy)] pub struct S288;
    impl S288 { pub fn m(&mut self) { println!("c288 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr288 { fn m(&self); }
    impl Tr288 for Box<S288> { fn m(&self) { println!("c288 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(mut v: Box<S288>) { v.m(); }
    pub fn drive() { probe(Box::new(S288)); }
}
pub mod c289 {
    #[derive(Clone, Copy)] pub struct S289;
    impl S289 { pub fn m(&mut self) { println!("c289 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr289 { fn m(&mut self); }
    impl Tr289 for S289 { fn m(&mut self) { println!("c289 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(mut v: Box<S289>) { v.m(); }
    pub fn drive() { probe(Box::new(S289)); }
}
pub mod c290 {
    #[derive(Clone, Copy)] pub struct S290;
    impl S290 { pub fn m(&mut self) { println!("c290 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr290 { fn m(&mut self); }
    impl Tr290 for &S290 { fn m(&mut self) { println!("c290 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(mut v: Box<S290>) { v.m(); }
    pub fn drive() { probe(Box::new(S290)); }
}
pub mod c291 {
    #[derive(Clone, Copy)] pub struct S291;
    impl S291 { pub fn m(&mut self) { println!("c291 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr291 { fn m(&mut self); }
    impl Tr291 for &mut S291 { fn m(&mut self) { println!("c291 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(mut v: Box<S291>) { v.m(); }
    pub fn drive() { probe(Box::new(S291)); }
}
pub mod c292 {
    #[derive(Clone, Copy)] pub struct S292;
    impl S292 { pub fn m(&mut self) { println!("c292 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr292 { fn m(&mut self); }
    impl Tr292 for Box<S292> { fn m(&mut self) { println!("c292 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(mut v: Box<S292>) { v.m(); }
    pub fn drive() { probe(Box::new(S292)); }
}
pub mod c293 {
    #[derive(Clone, Copy)] pub struct S293;
    pub trait Fa293 { fn m(self); }
    impl Fa293 for S293 { fn m(self) { println!("c293 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr293 { fn m(self); }
    impl Tr293 for &S293 { fn m(self) { println!("c293 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(mut v: Box<S293>) { v.m(); }
    pub fn drive() { probe(Box::new(S293)); }
}
pub mod c294 {
    #[derive(Clone, Copy)] pub struct S294;
    pub trait Fa294 { fn m(self); }
    impl Fa294 for S294 { fn m(self) { println!("c294 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr294 { fn m(self); }
    impl Tr294 for &mut S294 { fn m(self) { println!("c294 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(mut v: Box<S294>) { v.m(); }
    pub fn drive() { probe(Box::new(S294)); }
}
pub mod c295 {
    #[derive(Clone, Copy)] pub struct S295;
    pub trait Fa295 { fn m(self); }
    impl Fa295 for S295 { fn m(self) { println!("c295 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr295 { fn m(self); }
    impl Tr295 for Box<S295> { fn m(self) { println!("c295 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(mut v: Box<S295>) { v.m(); }
    pub fn drive() { probe(Box::new(S295)); }
}
pub mod c296 {
    #[derive(Clone, Copy)] pub struct S296;
    pub trait Fa296 { fn m(self); }
    impl Fa296 for S296 { fn m(self) { println!("c296 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr296 { fn m(&self); }
    impl Tr296 for S296 { fn m(&self) { println!("c296 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(mut v: Box<S296>) { v.m(); }
    pub fn drive() { probe(Box::new(S296)); }
}
pub mod c297 {
    #[derive(Clone, Copy)] pub struct S297;
    pub trait Fa297 { fn m(self); }
    impl Fa297 for S297 { fn m(self) { println!("c297 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr297 { fn m(&self); }
    impl Tr297 for &S297 { fn m(&self) { println!("c297 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(mut v: Box<S297>) { v.m(); }
    pub fn drive() { probe(Box::new(S297)); }
}
pub mod c298 {
    #[derive(Clone, Copy)] pub struct S298;
    pub trait Fa298 { fn m(self); }
    impl Fa298 for S298 { fn m(self) { println!("c298 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr298 { fn m(&self); }
    impl Tr298 for &mut S298 { fn m(&self) { println!("c298 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(mut v: Box<S298>) { v.m(); }
    pub fn drive() { probe(Box::new(S298)); }
}
pub mod c299 {
    #[derive(Clone, Copy)] pub struct S299;
    pub trait Fa299 { fn m(self); }
    impl Fa299 for S299 { fn m(self) { println!("c299 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr299 { fn m(&self); }
    impl Tr299 for Box<S299> { fn m(&self) { println!("c299 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(mut v: Box<S299>) { v.m(); }
    pub fn drive() { probe(Box::new(S299)); }
}
pub mod c300 {
    #[derive(Clone, Copy)] pub struct S300;
    pub trait Fa300 { fn m(self); }
    impl Fa300 for S300 { fn m(self) { println!("c300 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr300 { fn m(&mut self); }
    impl Tr300 for S300 { fn m(&mut self) { println!("c300 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(mut v: Box<S300>) { v.m(); }
    pub fn drive() { probe(Box::new(S300)); }
}
pub mod c301 {
    #[derive(Clone, Copy)] pub struct S301;
    pub trait Fa301 { fn m(self); }
    impl Fa301 for S301 { fn m(self) { println!("c301 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr301 { fn m(&mut self); }
    impl Tr301 for &S301 { fn m(&mut self) { println!("c301 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(mut v: Box<S301>) { v.m(); }
    pub fn drive() { probe(Box::new(S301)); }
}
pub mod c302 {
    #[derive(Clone, Copy)] pub struct S302;
    pub trait Fa302 { fn m(self); }
    impl Fa302 for S302 { fn m(self) { println!("c302 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr302 { fn m(&mut self); }
    impl Tr302 for &mut S302 { fn m(&mut self) { println!("c302 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(mut v: Box<S302>) { v.m(); }
    pub fn drive() { probe(Box::new(S302)); }
}
pub mod c303 {
    #[derive(Clone, Copy)] pub struct S303;
    pub trait Fa303 { fn m(self); }
    impl Fa303 for S303 { fn m(self) { println!("c303 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr303 { fn m(&mut self); }
    impl Tr303 for Box<S303> { fn m(&mut self) { println!("c303 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(mut v: Box<S303>) { v.m(); }
    pub fn drive() { probe(Box::new(S303)); }
}
pub mod c304 {
    #[derive(Clone, Copy)] pub struct S304;
    pub trait Fa304 { fn m(&self); }
    impl Fa304 for S304 { fn m(&self) { println!("c304 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr304 { fn m(self); }
    impl Tr304 for S304 { fn m(self) { println!("c304 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(mut v: Box<S304>) { v.m(); }
    pub fn drive() { probe(Box::new(S304)); }
}
pub mod c305 {
    #[derive(Clone, Copy)] pub struct S305;
    pub trait Fa305 { fn m(&self); }
    impl Fa305 for S305 { fn m(&self) { println!("c305 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr305 { fn m(self); }
    impl Tr305 for &mut S305 { fn m(self) { println!("c305 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(mut v: Box<S305>) { v.m(); }
    pub fn drive() { probe(Box::new(S305)); }
}
pub mod c306 {
    #[derive(Clone, Copy)] pub struct S306;
    pub trait Fa306 { fn m(&self); }
    impl Fa306 for S306 { fn m(&self) { println!("c306 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr306 { fn m(self); }
    impl Tr306 for Box<S306> { fn m(self) { println!("c306 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(mut v: Box<S306>) { v.m(); }
    pub fn drive() { probe(Box::new(S306)); }
}
pub mod c307 {
    #[derive(Clone, Copy)] pub struct S307;
    pub trait Fa307 { fn m(&self); }
    impl Fa307 for S307 { fn m(&self) { println!("c307 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr307 { fn m(&self); }
    impl Tr307 for &S307 { fn m(&self) { println!("c307 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(mut v: Box<S307>) { v.m(); }
    pub fn drive() { probe(Box::new(S307)); }
}
pub mod c308 {
    #[derive(Clone, Copy)] pub struct S308;
    pub trait Fa308 { fn m(&self); }
    impl Fa308 for S308 { fn m(&self) { println!("c308 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr308 { fn m(&self); }
    impl Tr308 for &mut S308 { fn m(&self) { println!("c308 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(mut v: Box<S308>) { v.m(); }
    pub fn drive() { probe(Box::new(S308)); }
}
pub mod c309 {
    #[derive(Clone, Copy)] pub struct S309;
    pub trait Fa309 { fn m(&self); }
    impl Fa309 for S309 { fn m(&self) { println!("c309 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr309 { fn m(&self); }
    impl Tr309 for Box<S309> { fn m(&self) { println!("c309 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(mut v: Box<S309>) { v.m(); }
    pub fn drive() { probe(Box::new(S309)); }
}
pub mod c310 {
    #[derive(Clone, Copy)] pub struct S310;
    pub trait Fa310 { fn m(&self); }
    impl Fa310 for S310 { fn m(&self) { println!("c310 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr310 { fn m(&mut self); }
    impl Tr310 for S310 { fn m(&mut self) { println!("c310 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(mut v: Box<S310>) { v.m(); }
    pub fn drive() { probe(Box::new(S310)); }
}
pub mod c311 {
    #[derive(Clone, Copy)] pub struct S311;
    pub trait Fa311 { fn m(&self); }
    impl Fa311 for S311 { fn m(&self) { println!("c311 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr311 { fn m(&mut self); }
    impl Tr311 for &S311 { fn m(&mut self) { println!("c311 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(mut v: Box<S311>) { v.m(); }
    pub fn drive() { probe(Box::new(S311)); }
}
pub mod c312 {
    #[derive(Clone, Copy)] pub struct S312;
    pub trait Fa312 { fn m(&self); }
    impl Fa312 for S312 { fn m(&self) { println!("c312 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr312 { fn m(&mut self); }
    impl Tr312 for &mut S312 { fn m(&mut self) { println!("c312 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(mut v: Box<S312>) { v.m(); }
    pub fn drive() { probe(Box::new(S312)); }
}
pub mod c313 {
    #[derive(Clone, Copy)] pub struct S313;
    pub trait Fa313 { fn m(&self); }
    impl Fa313 for S313 { fn m(&self) { println!("c313 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr313 { fn m(&mut self); }
    impl Tr313 for Box<S313> { fn m(&mut self) { println!("c313 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(mut v: Box<S313>) { v.m(); }
    pub fn drive() { probe(Box::new(S313)); }
}
pub mod c314 {
    #[derive(Clone, Copy)] pub struct S314;
    pub trait Fa314 { fn m(&mut self); }
    impl Fa314 for S314 { fn m(&mut self) { println!("c314 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr314 { fn m(self); }
    impl Tr314 for S314 { fn m(self) { println!("c314 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(mut v: Box<S314>) { v.m(); }
    pub fn drive() { probe(Box::new(S314)); }
}
pub mod c315 {
    #[derive(Clone, Copy)] pub struct S315;
    pub trait Fa315 { fn m(&mut self); }
    impl Fa315 for S315 { fn m(&mut self) { println!("c315 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr315 { fn m(self); }
    impl Tr315 for &S315 { fn m(self) { println!("c315 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(mut v: Box<S315>) { v.m(); }
    pub fn drive() { probe(Box::new(S315)); }
}
pub mod c316 {
    #[derive(Clone, Copy)] pub struct S316;
    pub trait Fa316 { fn m(&mut self); }
    impl Fa316 for S316 { fn m(&mut self) { println!("c316 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr316 { fn m(self); }
    impl Tr316 for Box<S316> { fn m(self) { println!("c316 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(mut v: Box<S316>) { v.m(); }
    pub fn drive() { probe(Box::new(S316)); }
}
pub mod c317 {
    #[derive(Clone, Copy)] pub struct S317;
    pub trait Fa317 { fn m(&mut self); }
    impl Fa317 for S317 { fn m(&mut self) { println!("c317 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr317 { fn m(&self); }
    impl Tr317 for S317 { fn m(&self) { println!("c317 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(mut v: Box<S317>) { v.m(); }
    pub fn drive() { probe(Box::new(S317)); }
}
pub mod c318 {
    #[derive(Clone, Copy)] pub struct S318;
    pub trait Fa318 { fn m(&mut self); }
    impl Fa318 for S318 { fn m(&mut self) { println!("c318 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr318 { fn m(&self); }
    impl Tr318 for &S318 { fn m(&self) { println!("c318 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(mut v: Box<S318>) { v.m(); }
    pub fn drive() { probe(Box::new(S318)); }
}
pub mod c319 {
    #[derive(Clone, Copy)] pub struct S319;
    pub trait Fa319 { fn m(&mut self); }
    impl Fa319 for S319 { fn m(&mut self) { println!("c319 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr319 { fn m(&self); }
    impl Tr319 for &mut S319 { fn m(&self) { println!("c319 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(mut v: Box<S319>) { v.m(); }
    pub fn drive() { probe(Box::new(S319)); }
}
pub mod c320 {
    #[derive(Clone, Copy)] pub struct S320;
    pub trait Fa320 { fn m(&mut self); }
    impl Fa320 for S320 { fn m(&mut self) { println!("c320 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr320 { fn m(&self); }
    impl Tr320 for Box<S320> { fn m(&self) { println!("c320 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(mut v: Box<S320>) { v.m(); }
    pub fn drive() { probe(Box::new(S320)); }
}
pub mod c321 {
    #[derive(Clone, Copy)] pub struct S321;
    pub trait Fa321 { fn m(&mut self); }
    impl Fa321 for S321 { fn m(&mut self) { println!("c321 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr321 { fn m(&mut self); }
    impl Tr321 for &S321 { fn m(&mut self) { println!("c321 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(mut v: Box<S321>) { v.m(); }
    pub fn drive() { probe(Box::new(S321)); }
}
pub mod c322 {
    #[derive(Clone, Copy)] pub struct S322;
    pub trait Fa322 { fn m(&mut self); }
    impl Fa322 for S322 { fn m(&mut self) { println!("c322 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr322 { fn m(&mut self); }
    impl Tr322 for &mut S322 { fn m(&mut self) { println!("c322 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(mut v: Box<S322>) { v.m(); }
    pub fn drive() { probe(Box::new(S322)); }
}
pub mod c323 {
    #[derive(Clone, Copy)] pub struct S323;
    pub trait Fa323 { fn m(&mut self); }
    impl Fa323 for S323 { fn m(&mut self) { println!("c323 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr323 { fn m(&mut self); }
    impl Tr323 for Box<S323> { fn m(&mut self) { println!("c323 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(mut v: Box<S323>) { v.m(); }
    pub fn drive() { probe(Box::new(S323)); }
}
pub mod c324 {
    #[derive(Clone, Copy)] pub struct S324;
    pub trait Fa324 { fn m(self); }
    impl Fa324 for &S324 { fn m(self) { println!("c324 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr324 { fn m(self); }
    impl Tr324 for S324 { fn m(self) { println!("c324 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(mut v: Box<S324>) { v.m(); }
    pub fn drive() { probe(Box::new(S324)); }
}
pub mod c325 {
    #[derive(Clone, Copy)] pub struct S325;
    pub trait Fa325 { fn m(self); }
    impl Fa325 for &S325 { fn m(self) { println!("c325 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr325 { fn m(self); }
    impl Tr325 for &mut S325 { fn m(self) { println!("c325 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(mut v: Box<S325>) { v.m(); }
    pub fn drive() { probe(Box::new(S325)); }
}
pub mod c326 {
    #[derive(Clone, Copy)] pub struct S326;
    pub trait Fa326 { fn m(self); }
    impl Fa326 for &S326 { fn m(self) { println!("c326 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr326 { fn m(self); }
    impl Tr326 for Box<S326> { fn m(self) { println!("c326 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(mut v: Box<S326>) { v.m(); }
    pub fn drive() { probe(Box::new(S326)); }
}
pub mod c327 {
    #[derive(Clone, Copy)] pub struct S327;
    pub trait Fa327 { fn m(self); }
    impl Fa327 for &S327 { fn m(self) { println!("c327 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr327 { fn m(&self); }
    impl Tr327 for &S327 { fn m(&self) { println!("c327 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(mut v: Box<S327>) { v.m(); }
    pub fn drive() { probe(Box::new(S327)); }
}
pub mod c328 {
    #[derive(Clone, Copy)] pub struct S328;
    pub trait Fa328 { fn m(self); }
    impl Fa328 for &S328 { fn m(self) { println!("c328 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr328 { fn m(&self); }
    impl Tr328 for &mut S328 { fn m(&self) { println!("c328 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(mut v: Box<S328>) { v.m(); }
    pub fn drive() { probe(Box::new(S328)); }
}
pub mod c329 {
    #[derive(Clone, Copy)] pub struct S329;
    pub trait Fa329 { fn m(self); }
    impl Fa329 for &S329 { fn m(self) { println!("c329 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr329 { fn m(&self); }
    impl Tr329 for Box<S329> { fn m(&self) { println!("c329 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(mut v: Box<S329>) { v.m(); }
    pub fn drive() { probe(Box::new(S329)); }
}
pub mod c330 {
    #[derive(Clone, Copy)] pub struct S330;
    pub trait Fa330 { fn m(self); }
    impl Fa330 for &S330 { fn m(self) { println!("c330 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr330 { fn m(&mut self); }
    impl Tr330 for S330 { fn m(&mut self) { println!("c330 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(mut v: Box<S330>) { v.m(); }
    pub fn drive() { probe(Box::new(S330)); }
}
pub mod c331 {
    #[derive(Clone, Copy)] pub struct S331;
    pub trait Fa331 { fn m(self); }
    impl Fa331 for &S331 { fn m(self) { println!("c331 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr331 { fn m(&mut self); }
    impl Tr331 for &S331 { fn m(&mut self) { println!("c331 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(mut v: Box<S331>) { v.m(); }
    pub fn drive() { probe(Box::new(S331)); }
}
pub mod c332 {
    #[derive(Clone, Copy)] pub struct S332;
    pub trait Fa332 { fn m(self); }
    impl Fa332 for &S332 { fn m(self) { println!("c332 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr332 { fn m(&mut self); }
    impl Tr332 for &mut S332 { fn m(&mut self) { println!("c332 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(mut v: Box<S332>) { v.m(); }
    pub fn drive() { probe(Box::new(S332)); }
}
pub mod c333 {
    #[derive(Clone, Copy)] pub struct S333;
    pub trait Fa333 { fn m(self); }
    impl Fa333 for &S333 { fn m(self) { println!("c333 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr333 { fn m(&mut self); }
    impl Tr333 for Box<S333> { fn m(&mut self) { println!("c333 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(mut v: Box<S333>) { v.m(); }
    pub fn drive() { probe(Box::new(S333)); }
}
pub mod c334 {
    #[derive(Clone, Copy)] pub struct S334;
    pub trait Fa334 { fn m(self); }
    impl Fa334 for &mut S334 { fn m(self) { println!("c334 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr334 { fn m(self); }
    impl Tr334 for S334 { fn m(self) { println!("c334 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(mut v: Box<S334>) { v.m(); }
    pub fn drive() { probe(Box::new(S334)); }
}
pub mod c335 {
    #[derive(Clone, Copy)] pub struct S335;
    pub trait Fa335 { fn m(self); }
    impl Fa335 for &mut S335 { fn m(self) { println!("c335 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr335 { fn m(self); }
    impl Tr335 for &S335 { fn m(self) { println!("c335 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(mut v: Box<S335>) { v.m(); }
    pub fn drive() { probe(Box::new(S335)); }
}
pub mod c336 {
    #[derive(Clone, Copy)] pub struct S336;
    pub trait Fa336 { fn m(self); }
    impl Fa336 for &mut S336 { fn m(self) { println!("c336 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr336 { fn m(self); }
    impl Tr336 for Box<S336> { fn m(self) { println!("c336 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(mut v: Box<S336>) { v.m(); }
    pub fn drive() { probe(Box::new(S336)); }
}
pub mod c337 {
    #[derive(Clone, Copy)] pub struct S337;
    pub trait Fa337 { fn m(self); }
    impl Fa337 for &mut S337 { fn m(self) { println!("c337 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr337 { fn m(&self); }
    impl Tr337 for S337 { fn m(&self) { println!("c337 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(mut v: Box<S337>) { v.m(); }
    pub fn drive() { probe(Box::new(S337)); }
}
pub mod c338 {
    #[derive(Clone, Copy)] pub struct S338;
    pub trait Fa338 { fn m(self); }
    impl Fa338 for &mut S338 { fn m(self) { println!("c338 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr338 { fn m(&self); }
    impl Tr338 for &S338 { fn m(&self) { println!("c338 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(mut v: Box<S338>) { v.m(); }
    pub fn drive() { probe(Box::new(S338)); }
}
pub mod c339 {
    #[derive(Clone, Copy)] pub struct S339;
    pub trait Fa339 { fn m(self); }
    impl Fa339 for &mut S339 { fn m(self) { println!("c339 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr339 { fn m(&self); }
    impl Tr339 for &mut S339 { fn m(&self) { println!("c339 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(mut v: Box<S339>) { v.m(); }
    pub fn drive() { probe(Box::new(S339)); }
}
pub mod c340 {
    #[derive(Clone, Copy)] pub struct S340;
    pub trait Fa340 { fn m(self); }
    impl Fa340 for &mut S340 { fn m(self) { println!("c340 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr340 { fn m(&self); }
    impl Tr340 for Box<S340> { fn m(&self) { println!("c340 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(mut v: Box<S340>) { v.m(); }
    pub fn drive() { probe(Box::new(S340)); }
}
pub mod c341 {
    #[derive(Clone, Copy)] pub struct S341;
    pub trait Fa341 { fn m(self); }
    impl Fa341 for &mut S341 { fn m(self) { println!("c341 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr341 { fn m(&mut self); }
    impl Tr341 for &S341 { fn m(&mut self) { println!("c341 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(mut v: Box<S341>) { v.m(); }
    pub fn drive() { probe(Box::new(S341)); }
}
pub mod c342 {
    #[derive(Clone, Copy)] pub struct S342;
    pub trait Fa342 { fn m(self); }
    impl Fa342 for &mut S342 { fn m(self) { println!("c342 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr342 { fn m(&mut self); }
    impl Tr342 for &mut S342 { fn m(&mut self) { println!("c342 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(mut v: Box<S342>) { v.m(); }
    pub fn drive() { probe(Box::new(S342)); }
}
pub mod c343 {
    #[derive(Clone, Copy)] pub struct S343;
    pub trait Fa343 { fn m(self); }
    impl Fa343 for &mut S343 { fn m(self) { println!("c343 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr343 { fn m(&mut self); }
    impl Tr343 for Box<S343> { fn m(&mut self) { println!("c343 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(mut v: Box<S343>) { v.m(); }
    pub fn drive() { probe(Box::new(S343)); }
}
pub mod c344 {
    #[derive(Clone, Copy)] pub struct S344;
    pub trait Tr344 { fn m(self); }
    impl Tr344 for S344 { fn m(self) { println!("c344 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &&S344) { v.m(); }
    pub fn drive() { probe(&&S344); }
}
pub mod c345 {
    #[derive(Clone, Copy)] pub struct S345;
    pub trait Tr345 { fn m(self); }
    impl Tr345 for &S345 { fn m(self) { println!("c345 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &&S345) { v.m(); }
    pub fn drive() { probe(&&S345); }
}
pub mod c346 {
    #[derive(Clone, Copy)] pub struct S346;
    pub trait Tr346 { fn m(&self); }
    impl Tr346 for S346 { fn m(&self) { println!("c346 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &&S346) { v.m(); }
    pub fn drive() { probe(&&S346); }
}
pub mod c347 {
    #[derive(Clone, Copy)] pub struct S347;
    pub trait Tr347 { fn m(&self); }
    impl Tr347 for &S347 { fn m(&self) { println!("c347 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &&S347) { v.m(); }
    pub fn drive() { probe(&&S347); }
}
pub mod c348 {
    #[derive(Clone, Copy)] pub struct S348;
    impl S348 { pub fn m(self) { println!("c348 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr348 { fn m(self); }
    impl Tr348 for S348 { fn m(self) { println!("c348 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &&S348) { v.m(); }
    pub fn drive() { probe(&&S348); }
}
pub mod c349 {
    #[derive(Clone, Copy)] pub struct S349;
    impl S349 { pub fn m(self) { println!("c349 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr349 { fn m(self); }
    impl Tr349 for &S349 { fn m(self) { println!("c349 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &&S349) { v.m(); }
    pub fn drive() { probe(&&S349); }
}
pub mod c350 {
    #[derive(Clone, Copy)] pub struct S350;
    impl S350 { pub fn m(self) { println!("c350 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr350 { fn m(self); }
    impl Tr350 for &mut S350 { fn m(self) { println!("c350 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &&S350) { v.m(); }
    pub fn drive() { probe(&&S350); }
}
pub mod c351 {
    #[derive(Clone, Copy)] pub struct S351;
    impl S351 { pub fn m(self) { println!("c351 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr351 { fn m(self); }
    impl Tr351 for Box<S351> { fn m(self) { println!("c351 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &&S351) { v.m(); }
    pub fn drive() { probe(&&S351); }
}
pub mod c352 {
    #[derive(Clone, Copy)] pub struct S352;
    impl S352 { pub fn m(self) { println!("c352 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr352 { fn m(&self); }
    impl Tr352 for S352 { fn m(&self) { println!("c352 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &&S352) { v.m(); }
    pub fn drive() { probe(&&S352); }
}
pub mod c353 {
    #[derive(Clone, Copy)] pub struct S353;
    impl S353 { pub fn m(self) { println!("c353 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr353 { fn m(&self); }
    impl Tr353 for &S353 { fn m(&self) { println!("c353 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &&S353) { v.m(); }
    pub fn drive() { probe(&&S353); }
}
pub mod c354 {
    #[derive(Clone, Copy)] pub struct S354;
    impl S354 { pub fn m(self) { println!("c354 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr354 { fn m(&self); }
    impl Tr354 for &mut S354 { fn m(&self) { println!("c354 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &&S354) { v.m(); }
    pub fn drive() { probe(&&S354); }
}
pub mod c355 {
    #[derive(Clone, Copy)] pub struct S355;
    impl S355 { pub fn m(self) { println!("c355 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr355 { fn m(&self); }
    impl Tr355 for Box<S355> { fn m(&self) { println!("c355 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &&S355) { v.m(); }
    pub fn drive() { probe(&&S355); }
}
pub mod c356 {
    #[derive(Clone, Copy)] pub struct S356;
    impl S356 { pub fn m(self) { println!("c356 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr356 { fn m(&mut self); }
    impl Tr356 for S356 { fn m(&mut self) { println!("c356 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &&S356) { v.m(); }
    pub fn drive() { probe(&&S356); }
}
pub mod c357 {
    #[derive(Clone, Copy)] pub struct S357;
    impl S357 { pub fn m(self) { println!("c357 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr357 { fn m(&mut self); }
    impl Tr357 for &mut S357 { fn m(&mut self) { println!("c357 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &&S357) { v.m(); }
    pub fn drive() { probe(&&S357); }
}
pub mod c358 {
    #[derive(Clone, Copy)] pub struct S358;
    impl S358 { pub fn m(self) { println!("c358 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr358 { fn m(&mut self); }
    impl Tr358 for Box<S358> { fn m(&mut self) { println!("c358 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &&S358) { v.m(); }
    pub fn drive() { probe(&&S358); }
}
pub mod c359 {
    #[derive(Clone, Copy)] pub struct S359;
    impl S359 { pub fn m(&self) { println!("c359 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr359 { fn m(self); }
    impl Tr359 for S359 { fn m(self) { println!("c359 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &&S359) { v.m(); }
    pub fn drive() { probe(&&S359); }
}
pub mod c360 {
    #[derive(Clone, Copy)] pub struct S360;
    impl S360 { pub fn m(&self) { println!("c360 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr360 { fn m(self); }
    impl Tr360 for &S360 { fn m(self) { println!("c360 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &&S360) { v.m(); }
    pub fn drive() { probe(&&S360); }
}
pub mod c361 {
    #[derive(Clone, Copy)] pub struct S361;
    impl S361 { pub fn m(&self) { println!("c361 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr361 { fn m(self); }
    impl Tr361 for &mut S361 { fn m(self) { println!("c361 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &&S361) { v.m(); }
    pub fn drive() { probe(&&S361); }
}
pub mod c362 {
    #[derive(Clone, Copy)] pub struct S362;
    impl S362 { pub fn m(&self) { println!("c362 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr362 { fn m(self); }
    impl Tr362 for Box<S362> { fn m(self) { println!("c362 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &&S362) { v.m(); }
    pub fn drive() { probe(&&S362); }
}
pub mod c363 {
    #[derive(Clone, Copy)] pub struct S363;
    impl S363 { pub fn m(&self) { println!("c363 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr363 { fn m(&self); }
    impl Tr363 for S363 { fn m(&self) { println!("c363 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &&S363) { v.m(); }
    pub fn drive() { probe(&&S363); }
}
pub mod c364 {
    #[derive(Clone, Copy)] pub struct S364;
    impl S364 { pub fn m(&self) { println!("c364 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr364 { fn m(&self); }
    impl Tr364 for &S364 { fn m(&self) { println!("c364 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &&S364) { v.m(); }
    pub fn drive() { probe(&&S364); }
}
pub mod c365 {
    #[derive(Clone, Copy)] pub struct S365;
    impl S365 { pub fn m(&self) { println!("c365 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr365 { fn m(&self); }
    impl Tr365 for &mut S365 { fn m(&self) { println!("c365 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &&S365) { v.m(); }
    pub fn drive() { probe(&&S365); }
}
pub mod c366 {
    #[derive(Clone, Copy)] pub struct S366;
    impl S366 { pub fn m(&self) { println!("c366 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr366 { fn m(&self); }
    impl Tr366 for Box<S366> { fn m(&self) { println!("c366 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &&S366) { v.m(); }
    pub fn drive() { probe(&&S366); }
}
pub mod c367 {
    #[derive(Clone, Copy)] pub struct S367;
    impl S367 { pub fn m(&self) { println!("c367 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr367 { fn m(&mut self); }
    impl Tr367 for S367 { fn m(&mut self) { println!("c367 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &&S367) { v.m(); }
    pub fn drive() { probe(&&S367); }
}
pub mod c368 {
    #[derive(Clone, Copy)] pub struct S368;
    impl S368 { pub fn m(&self) { println!("c368 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr368 { fn m(&mut self); }
    impl Tr368 for &S368 { fn m(&mut self) { println!("c368 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &&S368) { v.m(); }
    pub fn drive() { probe(&&S368); }
}
pub mod c369 {
    #[derive(Clone, Copy)] pub struct S369;
    impl S369 { pub fn m(&self) { println!("c369 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr369 { fn m(&mut self); }
    impl Tr369 for &mut S369 { fn m(&mut self) { println!("c369 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &&S369) { v.m(); }
    pub fn drive() { probe(&&S369); }
}
pub mod c370 {
    #[derive(Clone, Copy)] pub struct S370;
    impl S370 { pub fn m(&self) { println!("c370 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr370 { fn m(&mut self); }
    impl Tr370 for Box<S370> { fn m(&mut self) { println!("c370 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &&S370) { v.m(); }
    pub fn drive() { probe(&&S370); }
}
pub mod c371 {
    #[derive(Clone, Copy)] pub struct S371;
    impl S371 { pub fn m(&mut self) { println!("c371 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr371 { fn m(self); }
    impl Tr371 for S371 { fn m(self) { println!("c371 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &&S371) { v.m(); }
    pub fn drive() { probe(&&S371); }
}
pub mod c372 {
    #[derive(Clone, Copy)] pub struct S372;
    impl S372 { pub fn m(&mut self) { println!("c372 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr372 { fn m(self); }
    impl Tr372 for &S372 { fn m(self) { println!("c372 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &&S372) { v.m(); }
    pub fn drive() { probe(&&S372); }
}
pub mod c373 {
    #[derive(Clone, Copy)] pub struct S373;
    impl S373 { pub fn m(&mut self) { println!("c373 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr373 { fn m(&self); }
    impl Tr373 for S373 { fn m(&self) { println!("c373 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &&S373) { v.m(); }
    pub fn drive() { probe(&&S373); }
}
pub mod c374 {
    #[derive(Clone, Copy)] pub struct S374;
    impl S374 { pub fn m(&mut self) { println!("c374 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr374 { fn m(&self); }
    impl Tr374 for &S374 { fn m(&self) { println!("c374 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &&S374) { v.m(); }
    pub fn drive() { probe(&&S374); }
}
pub mod c375 {
    #[derive(Clone, Copy)] pub struct S375;
    pub trait Fa375 { fn m(self); }
    impl Fa375 for S375 { fn m(self) { println!("c375 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr375 { fn m(self); }
    impl Tr375 for &S375 { fn m(self) { println!("c375 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &&S375) { v.m(); }
    pub fn drive() { probe(&&S375); }
}
pub mod c376 {
    #[derive(Clone, Copy)] pub struct S376;
    pub trait Fa376 { fn m(self); }
    impl Fa376 for S376 { fn m(self) { println!("c376 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr376 { fn m(self); }
    impl Tr376 for &mut S376 { fn m(self) { println!("c376 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &&S376) { v.m(); }
    pub fn drive() { probe(&&S376); }
}
pub mod c377 {
    #[derive(Clone, Copy)] pub struct S377;
    pub trait Fa377 { fn m(self); }
    impl Fa377 for S377 { fn m(self) { println!("c377 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr377 { fn m(self); }
    impl Tr377 for Box<S377> { fn m(self) { println!("c377 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &&S377) { v.m(); }
    pub fn drive() { probe(&&S377); }
}
pub mod c378 {
    #[derive(Clone, Copy)] pub struct S378;
    pub trait Fa378 { fn m(self); }
    impl Fa378 for S378 { fn m(self) { println!("c378 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr378 { fn m(&self); }
    impl Tr378 for S378 { fn m(&self) { println!("c378 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &&S378) { v.m(); }
    pub fn drive() { probe(&&S378); }
}
pub mod c379 {
    #[derive(Clone, Copy)] pub struct S379;
    pub trait Fa379 { fn m(self); }
    impl Fa379 for S379 { fn m(self) { println!("c379 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr379 { fn m(&self); }
    impl Tr379 for &S379 { fn m(&self) { println!("c379 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &&S379) { v.m(); }
    pub fn drive() { probe(&&S379); }
}
pub mod c380 {
    #[derive(Clone, Copy)] pub struct S380;
    pub trait Fa380 { fn m(self); }
    impl Fa380 for S380 { fn m(self) { println!("c380 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr380 { fn m(&self); }
    impl Tr380 for &mut S380 { fn m(&self) { println!("c380 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &&S380) { v.m(); }
    pub fn drive() { probe(&&S380); }
}
pub mod c381 {
    #[derive(Clone, Copy)] pub struct S381;
    pub trait Fa381 { fn m(self); }
    impl Fa381 for S381 { fn m(self) { println!("c381 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr381 { fn m(&self); }
    impl Tr381 for Box<S381> { fn m(&self) { println!("c381 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &&S381) { v.m(); }
    pub fn drive() { probe(&&S381); }
}
pub mod c382 {
    #[derive(Clone, Copy)] pub struct S382;
    pub trait Fa382 { fn m(self); }
    impl Fa382 for S382 { fn m(self) { println!("c382 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr382 { fn m(&mut self); }
    impl Tr382 for S382 { fn m(&mut self) { println!("c382 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &&S382) { v.m(); }
    pub fn drive() { probe(&&S382); }
}
pub mod c383 {
    #[derive(Clone, Copy)] pub struct S383;
    pub trait Fa383 { fn m(self); }
    impl Fa383 for S383 { fn m(self) { println!("c383 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr383 { fn m(&mut self); }
    impl Tr383 for &mut S383 { fn m(&mut self) { println!("c383 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &&S383) { v.m(); }
    pub fn drive() { probe(&&S383); }
}
pub mod c384 {
    #[derive(Clone, Copy)] pub struct S384;
    pub trait Fa384 { fn m(self); }
    impl Fa384 for S384 { fn m(self) { println!("c384 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr384 { fn m(&mut self); }
    impl Tr384 for Box<S384> { fn m(&mut self) { println!("c384 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &&S384) { v.m(); }
    pub fn drive() { probe(&&S384); }
}
pub mod c385 {
    #[derive(Clone, Copy)] pub struct S385;
    pub trait Fa385 { fn m(&self); }
    impl Fa385 for S385 { fn m(&self) { println!("c385 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr385 { fn m(self); }
    impl Tr385 for S385 { fn m(self) { println!("c385 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &&S385) { v.m(); }
    pub fn drive() { probe(&&S385); }
}
pub mod c386 {
    #[derive(Clone, Copy)] pub struct S386;
    pub trait Fa386 { fn m(&self); }
    impl Fa386 for S386 { fn m(&self) { println!("c386 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr386 { fn m(self); }
    impl Tr386 for &mut S386 { fn m(self) { println!("c386 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &&S386) { v.m(); }
    pub fn drive() { probe(&&S386); }
}
pub mod c387 {
    #[derive(Clone, Copy)] pub struct S387;
    pub trait Fa387 { fn m(&self); }
    impl Fa387 for S387 { fn m(&self) { println!("c387 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr387 { fn m(self); }
    impl Tr387 for Box<S387> { fn m(self) { println!("c387 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &&S387) { v.m(); }
    pub fn drive() { probe(&&S387); }
}
pub mod c388 {
    #[derive(Clone, Copy)] pub struct S388;
    pub trait Fa388 { fn m(&self); }
    impl Fa388 for S388 { fn m(&self) { println!("c388 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr388 { fn m(&self); }
    impl Tr388 for &S388 { fn m(&self) { println!("c388 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &&S388) { v.m(); }
    pub fn drive() { probe(&&S388); }
}
pub mod c389 {
    #[derive(Clone, Copy)] pub struct S389;
    pub trait Fa389 { fn m(&self); }
    impl Fa389 for S389 { fn m(&self) { println!("c389 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr389 { fn m(&self); }
    impl Tr389 for &mut S389 { fn m(&self) { println!("c389 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &&S389) { v.m(); }
    pub fn drive() { probe(&&S389); }
}
pub mod c390 {
    #[derive(Clone, Copy)] pub struct S390;
    pub trait Fa390 { fn m(&self); }
    impl Fa390 for S390 { fn m(&self) { println!("c390 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr390 { fn m(&self); }
    impl Tr390 for Box<S390> { fn m(&self) { println!("c390 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &&S390) { v.m(); }
    pub fn drive() { probe(&&S390); }
}
pub mod c391 {
    #[derive(Clone, Copy)] pub struct S391;
    pub trait Fa391 { fn m(&self); }
    impl Fa391 for S391 { fn m(&self) { println!("c391 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr391 { fn m(&mut self); }
    impl Tr391 for S391 { fn m(&mut self) { println!("c391 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &&S391) { v.m(); }
    pub fn drive() { probe(&&S391); }
}
pub mod c392 {
    #[derive(Clone, Copy)] pub struct S392;
    pub trait Fa392 { fn m(&self); }
    impl Fa392 for S392 { fn m(&self) { println!("c392 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr392 { fn m(&mut self); }
    impl Tr392 for &S392 { fn m(&mut self) { println!("c392 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &&S392) { v.m(); }
    pub fn drive() { probe(&&S392); }
}
pub mod c393 {
    #[derive(Clone, Copy)] pub struct S393;
    pub trait Fa393 { fn m(&self); }
    impl Fa393 for S393 { fn m(&self) { println!("c393 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr393 { fn m(&mut self); }
    impl Tr393 for &mut S393 { fn m(&mut self) { println!("c393 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &&S393) { v.m(); }
    pub fn drive() { probe(&&S393); }
}
pub mod c394 {
    #[derive(Clone, Copy)] pub struct S394;
    pub trait Fa394 { fn m(&self); }
    impl Fa394 for S394 { fn m(&self) { println!("c394 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr394 { fn m(&mut self); }
    impl Tr394 for Box<S394> { fn m(&mut self) { println!("c394 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &&S394) { v.m(); }
    pub fn drive() { probe(&&S394); }
}
pub mod c395 {
    #[derive(Clone, Copy)] pub struct S395;
    pub trait Fa395 { fn m(&mut self); }
    impl Fa395 for S395 { fn m(&mut self) { println!("c395 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr395 { fn m(self); }
    impl Tr395 for S395 { fn m(self) { println!("c395 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &&S395) { v.m(); }
    pub fn drive() { probe(&&S395); }
}
pub mod c396 {
    #[derive(Clone, Copy)] pub struct S396;
    pub trait Fa396 { fn m(&mut self); }
    impl Fa396 for S396 { fn m(&mut self) { println!("c396 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr396 { fn m(self); }
    impl Tr396 for &S396 { fn m(self) { println!("c396 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &&S396) { v.m(); }
    pub fn drive() { probe(&&S396); }
}
pub mod c397 {
    #[derive(Clone, Copy)] pub struct S397;
    pub trait Fa397 { fn m(&mut self); }
    impl Fa397 for S397 { fn m(&mut self) { println!("c397 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr397 { fn m(&self); }
    impl Tr397 for S397 { fn m(&self) { println!("c397 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &&S397) { v.m(); }
    pub fn drive() { probe(&&S397); }
}
pub mod c398 {
    #[derive(Clone, Copy)] pub struct S398;
    pub trait Fa398 { fn m(&mut self); }
    impl Fa398 for S398 { fn m(&mut self) { println!("c398 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr398 { fn m(&self); }
    impl Tr398 for &S398 { fn m(&self) { println!("c398 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &&S398) { v.m(); }
    pub fn drive() { probe(&&S398); }
}
pub mod c399 {
    #[derive(Clone, Copy)] pub struct S399;
    pub trait Fa399 { fn m(self); }
    impl Fa399 for &S399 { fn m(self) { println!("c399 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr399 { fn m(self); }
    impl Tr399 for S399 { fn m(self) { println!("c399 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &&S399) { v.m(); }
    pub fn drive() { probe(&&S399); }
}
pub mod c400 {
    #[derive(Clone, Copy)] pub struct S400;
    pub trait Fa400 { fn m(self); }
    impl Fa400 for &S400 { fn m(self) { println!("c400 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr400 { fn m(self); }
    impl Tr400 for &mut S400 { fn m(self) { println!("c400 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &&S400) { v.m(); }
    pub fn drive() { probe(&&S400); }
}
pub mod c401 {
    #[derive(Clone, Copy)] pub struct S401;
    pub trait Fa401 { fn m(self); }
    impl Fa401 for &S401 { fn m(self) { println!("c401 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr401 { fn m(self); }
    impl Tr401 for Box<S401> { fn m(self) { println!("c401 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &&S401) { v.m(); }
    pub fn drive() { probe(&&S401); }
}
pub mod c402 {
    #[derive(Clone, Copy)] pub struct S402;
    pub trait Fa402 { fn m(self); }
    impl Fa402 for &S402 { fn m(self) { println!("c402 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr402 { fn m(&self); }
    impl Tr402 for &S402 { fn m(&self) { println!("c402 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &&S402) { v.m(); }
    pub fn drive() { probe(&&S402); }
}
pub mod c403 {
    #[derive(Clone, Copy)] pub struct S403;
    pub trait Fa403 { fn m(self); }
    impl Fa403 for &S403 { fn m(self) { println!("c403 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr403 { fn m(&self); }
    impl Tr403 for &mut S403 { fn m(&self) { println!("c403 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &&S403) { v.m(); }
    pub fn drive() { probe(&&S403); }
}
pub mod c404 {
    #[derive(Clone, Copy)] pub struct S404;
    pub trait Fa404 { fn m(self); }
    impl Fa404 for &S404 { fn m(self) { println!("c404 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr404 { fn m(&self); }
    impl Tr404 for Box<S404> { fn m(&self) { println!("c404 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &&S404) { v.m(); }
    pub fn drive() { probe(&&S404); }
}
pub mod c405 {
    #[derive(Clone, Copy)] pub struct S405;
    pub trait Fa405 { fn m(self); }
    impl Fa405 for &S405 { fn m(self) { println!("c405 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr405 { fn m(&mut self); }
    impl Tr405 for S405 { fn m(&mut self) { println!("c405 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &&S405) { v.m(); }
    pub fn drive() { probe(&&S405); }
}
pub mod c406 {
    #[derive(Clone, Copy)] pub struct S406;
    pub trait Fa406 { fn m(self); }
    impl Fa406 for &S406 { fn m(self) { println!("c406 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr406 { fn m(&mut self); }
    impl Tr406 for &S406 { fn m(&mut self) { println!("c406 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &&S406) { v.m(); }
    pub fn drive() { probe(&&S406); }
}
pub mod c407 {
    #[derive(Clone, Copy)] pub struct S407;
    pub trait Fa407 { fn m(self); }
    impl Fa407 for &S407 { fn m(self) { println!("c407 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr407 { fn m(&mut self); }
    impl Tr407 for &mut S407 { fn m(&mut self) { println!("c407 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &&S407) { v.m(); }
    pub fn drive() { probe(&&S407); }
}
pub mod c408 {
    #[derive(Clone, Copy)] pub struct S408;
    pub trait Fa408 { fn m(self); }
    impl Fa408 for &S408 { fn m(self) { println!("c408 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr408 { fn m(&mut self); }
    impl Tr408 for Box<S408> { fn m(&mut self) { println!("c408 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &&S408) { v.m(); }
    pub fn drive() { probe(&&S408); }
}
pub mod c409 {
    #[derive(Clone, Copy)] pub struct S409;
    pub trait Fa409 { fn m(self); }
    impl Fa409 for &mut S409 { fn m(self) { println!("c409 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr409 { fn m(self); }
    impl Tr409 for S409 { fn m(self) { println!("c409 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &&S409) { v.m(); }
    pub fn drive() { probe(&&S409); }
}
pub mod c410 {
    #[derive(Clone, Copy)] pub struct S410;
    pub trait Fa410 { fn m(self); }
    impl Fa410 for &mut S410 { fn m(self) { println!("c410 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr410 { fn m(self); }
    impl Tr410 for &S410 { fn m(self) { println!("c410 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &&S410) { v.m(); }
    pub fn drive() { probe(&&S410); }
}
pub mod c411 {
    #[derive(Clone, Copy)] pub struct S411;
    pub trait Fa411 { fn m(self); }
    impl Fa411 for &mut S411 { fn m(self) { println!("c411 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr411 { fn m(&self); }
    impl Tr411 for S411 { fn m(&self) { println!("c411 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &&S411) { v.m(); }
    pub fn drive() { probe(&&S411); }
}
pub mod c412 {
    #[derive(Clone, Copy)] pub struct S412;
    pub trait Fa412 { fn m(self); }
    impl Fa412 for &mut S412 { fn m(self) { println!("c412 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr412 { fn m(&self); }
    impl Tr412 for &S412 { fn m(&self) { println!("c412 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &&S412) { v.m(); }
    pub fn drive() { probe(&&S412); }
}
pub mod c413 {
    #[derive(Clone, Copy)] pub struct S413;
    pub trait Tr413 { fn m(self); }
    impl Tr413 for S413 { fn m(self) { println!("c413 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &Box<S413>) { v.m(); }
    pub fn drive() { probe(&Box::new(S413)); }
}
pub mod c414 {
    #[derive(Clone, Copy)] pub struct S414;
    pub trait Tr414 { fn m(self); }
    impl Tr414 for &S414 { fn m(self) { println!("c414 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &Box<S414>) { v.m(); }
    pub fn drive() { probe(&Box::new(S414)); }
}
pub mod c415 {
    #[derive(Clone, Copy)] pub struct S415;
    pub trait Tr415 { fn m(&self); }
    impl Tr415 for S415 { fn m(&self) { println!("c415 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &Box<S415>) { v.m(); }
    pub fn drive() { probe(&Box::new(S415)); }
}
pub mod c416 {
    #[derive(Clone, Copy)] pub struct S416;
    pub trait Tr416 { fn m(&self); }
    impl Tr416 for Box<S416> { fn m(&self) { println!("c416 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &Box<S416>) { v.m(); }
    pub fn drive() { probe(&Box::new(S416)); }
}
pub mod c417 {
    #[derive(Clone, Copy)] pub struct S417;
    impl S417 { pub fn m(self) { println!("c417 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr417 { fn m(self); }
    impl Tr417 for S417 { fn m(self) { println!("c417 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &Box<S417>) { v.m(); }
    pub fn drive() { probe(&Box::new(S417)); }
}
pub mod c418 {
    #[derive(Clone, Copy)] pub struct S418;
    impl S418 { pub fn m(self) { println!("c418 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr418 { fn m(self); }
    impl Tr418 for &S418 { fn m(self) { println!("c418 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &Box<S418>) { v.m(); }
    pub fn drive() { probe(&Box::new(S418)); }
}
pub mod c419 {
    #[derive(Clone, Copy)] pub struct S419;
    impl S419 { pub fn m(self) { println!("c419 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr419 { fn m(self); }
    impl Tr419 for &mut S419 { fn m(self) { println!("c419 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &Box<S419>) { v.m(); }
    pub fn drive() { probe(&Box::new(S419)); }
}
pub mod c420 {
    #[derive(Clone, Copy)] pub struct S420;
    impl S420 { pub fn m(self) { println!("c420 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr420 { fn m(&self); }
    impl Tr420 for S420 { fn m(&self) { println!("c420 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &Box<S420>) { v.m(); }
    pub fn drive() { probe(&Box::new(S420)); }
}
pub mod c421 {
    #[derive(Clone, Copy)] pub struct S421;
    impl S421 { pub fn m(self) { println!("c421 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr421 { fn m(&self); }
    impl Tr421 for &S421 { fn m(&self) { println!("c421 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &Box<S421>) { v.m(); }
    pub fn drive() { probe(&Box::new(S421)); }
}
pub mod c422 {
    #[derive(Clone, Copy)] pub struct S422;
    impl S422 { pub fn m(self) { println!("c422 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr422 { fn m(&self); }
    impl Tr422 for &mut S422 { fn m(&self) { println!("c422 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &Box<S422>) { v.m(); }
    pub fn drive() { probe(&Box::new(S422)); }
}
pub mod c423 {
    #[derive(Clone, Copy)] pub struct S423;
    impl S423 { pub fn m(self) { println!("c423 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr423 { fn m(&self); }
    impl Tr423 for Box<S423> { fn m(&self) { println!("c423 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &Box<S423>) { v.m(); }
    pub fn drive() { probe(&Box::new(S423)); }
}
pub mod c424 {
    #[derive(Clone, Copy)] pub struct S424;
    impl S424 { pub fn m(self) { println!("c424 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr424 { fn m(&mut self); }
    impl Tr424 for S424 { fn m(&mut self) { println!("c424 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &Box<S424>) { v.m(); }
    pub fn drive() { probe(&Box::new(S424)); }
}
pub mod c425 {
    #[derive(Clone, Copy)] pub struct S425;
    impl S425 { pub fn m(self) { println!("c425 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr425 { fn m(&mut self); }
    impl Tr425 for &S425 { fn m(&mut self) { println!("c425 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &Box<S425>) { v.m(); }
    pub fn drive() { probe(&Box::new(S425)); }
}
pub mod c426 {
    #[derive(Clone, Copy)] pub struct S426;
    impl S426 { pub fn m(self) { println!("c426 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr426 { fn m(&mut self); }
    impl Tr426 for &mut S426 { fn m(&mut self) { println!("c426 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &Box<S426>) { v.m(); }
    pub fn drive() { probe(&Box::new(S426)); }
}
pub mod c427 {
    #[derive(Clone, Copy)] pub struct S427;
    impl S427 { pub fn m(&self) { println!("c427 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr427 { fn m(self); }
    impl Tr427 for S427 { fn m(self) { println!("c427 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &Box<S427>) { v.m(); }
    pub fn drive() { probe(&Box::new(S427)); }
}
pub mod c428 {
    #[derive(Clone, Copy)] pub struct S428;
    impl S428 { pub fn m(&self) { println!("c428 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr428 { fn m(self); }
    impl Tr428 for &S428 { fn m(self) { println!("c428 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &Box<S428>) { v.m(); }
    pub fn drive() { probe(&Box::new(S428)); }
}
pub mod c429 {
    #[derive(Clone, Copy)] pub struct S429;
    impl S429 { pub fn m(&self) { println!("c429 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr429 { fn m(self); }
    impl Tr429 for &mut S429 { fn m(self) { println!("c429 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &Box<S429>) { v.m(); }
    pub fn drive() { probe(&Box::new(S429)); }
}
pub mod c430 {
    #[derive(Clone, Copy)] pub struct S430;
    impl S430 { pub fn m(&self) { println!("c430 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr430 { fn m(&self); }
    impl Tr430 for S430 { fn m(&self) { println!("c430 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &Box<S430>) { v.m(); }
    pub fn drive() { probe(&Box::new(S430)); }
}
pub mod c431 {
    #[derive(Clone, Copy)] pub struct S431;
    impl S431 { pub fn m(&self) { println!("c431 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr431 { fn m(&self); }
    impl Tr431 for &S431 { fn m(&self) { println!("c431 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &Box<S431>) { v.m(); }
    pub fn drive() { probe(&Box::new(S431)); }
}
pub mod c432 {
    #[derive(Clone, Copy)] pub struct S432;
    impl S432 { pub fn m(&self) { println!("c432 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr432 { fn m(&self); }
    impl Tr432 for &mut S432 { fn m(&self) { println!("c432 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &Box<S432>) { v.m(); }
    pub fn drive() { probe(&Box::new(S432)); }
}
pub mod c433 {
    #[derive(Clone, Copy)] pub struct S433;
    impl S433 { pub fn m(&self) { println!("c433 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr433 { fn m(&self); }
    impl Tr433 for Box<S433> { fn m(&self) { println!("c433 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &Box<S433>) { v.m(); }
    pub fn drive() { probe(&Box::new(S433)); }
}
pub mod c434 {
    #[derive(Clone, Copy)] pub struct S434;
    impl S434 { pub fn m(&self) { println!("c434 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr434 { fn m(&mut self); }
    impl Tr434 for S434 { fn m(&mut self) { println!("c434 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &Box<S434>) { v.m(); }
    pub fn drive() { probe(&Box::new(S434)); }
}
pub mod c435 {
    #[derive(Clone, Copy)] pub struct S435;
    impl S435 { pub fn m(&self) { println!("c435 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr435 { fn m(&mut self); }
    impl Tr435 for &S435 { fn m(&mut self) { println!("c435 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &Box<S435>) { v.m(); }
    pub fn drive() { probe(&Box::new(S435)); }
}
pub mod c436 {
    #[derive(Clone, Copy)] pub struct S436;
    impl S436 { pub fn m(&self) { println!("c436 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr436 { fn m(&mut self); }
    impl Tr436 for &mut S436 { fn m(&mut self) { println!("c436 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &Box<S436>) { v.m(); }
    pub fn drive() { probe(&Box::new(S436)); }
}
pub mod c437 {
    #[derive(Clone, Copy)] pub struct S437;
    impl S437 { pub fn m(&mut self) { println!("c437 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr437 { fn m(self); }
    impl Tr437 for S437 { fn m(self) { println!("c437 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &Box<S437>) { v.m(); }
    pub fn drive() { probe(&Box::new(S437)); }
}
pub mod c438 {
    #[derive(Clone, Copy)] pub struct S438;
    impl S438 { pub fn m(&mut self) { println!("c438 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr438 { fn m(self); }
    impl Tr438 for &S438 { fn m(self) { println!("c438 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &Box<S438>) { v.m(); }
    pub fn drive() { probe(&Box::new(S438)); }
}
pub mod c439 {
    #[derive(Clone, Copy)] pub struct S439;
    impl S439 { pub fn m(&mut self) { println!("c439 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr439 { fn m(&self); }
    impl Tr439 for S439 { fn m(&self) { println!("c439 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &Box<S439>) { v.m(); }
    pub fn drive() { probe(&Box::new(S439)); }
}
pub mod c440 {
    #[derive(Clone, Copy)] pub struct S440;
    impl S440 { pub fn m(&mut self) { println!("c440 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr440 { fn m(&self); }
    impl Tr440 for Box<S440> { fn m(&self) { println!("c440 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &Box<S440>) { v.m(); }
    pub fn drive() { probe(&Box::new(S440)); }
}
pub mod c441 {
    #[derive(Clone, Copy)] pub struct S441;
    pub trait Fa441 { fn m(self); }
    impl Fa441 for S441 { fn m(self) { println!("c441 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr441 { fn m(self); }
    impl Tr441 for &S441 { fn m(self) { println!("c441 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &Box<S441>) { v.m(); }
    pub fn drive() { probe(&Box::new(S441)); }
}
pub mod c442 {
    #[derive(Clone, Copy)] pub struct S442;
    pub trait Fa442 { fn m(self); }
    impl Fa442 for S442 { fn m(self) { println!("c442 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr442 { fn m(self); }
    impl Tr442 for &mut S442 { fn m(self) { println!("c442 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &Box<S442>) { v.m(); }
    pub fn drive() { probe(&Box::new(S442)); }
}
pub mod c443 {
    #[derive(Clone, Copy)] pub struct S443;
    pub trait Fa443 { fn m(self); }
    impl Fa443 for S443 { fn m(self) { println!("c443 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr443 { fn m(&self); }
    impl Tr443 for S443 { fn m(&self) { println!("c443 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &Box<S443>) { v.m(); }
    pub fn drive() { probe(&Box::new(S443)); }
}
pub mod c444 {
    #[derive(Clone, Copy)] pub struct S444;
    pub trait Fa444 { fn m(self); }
    impl Fa444 for S444 { fn m(self) { println!("c444 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr444 { fn m(&self); }
    impl Tr444 for &S444 { fn m(&self) { println!("c444 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &Box<S444>) { v.m(); }
    pub fn drive() { probe(&Box::new(S444)); }
}
pub mod c445 {
    #[derive(Clone, Copy)] pub struct S445;
    pub trait Fa445 { fn m(self); }
    impl Fa445 for S445 { fn m(self) { println!("c445 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr445 { fn m(&self); }
    impl Tr445 for &mut S445 { fn m(&self) { println!("c445 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &Box<S445>) { v.m(); }
    pub fn drive() { probe(&Box::new(S445)); }
}
pub mod c446 {
    #[derive(Clone, Copy)] pub struct S446;
    pub trait Fa446 { fn m(self); }
    impl Fa446 for S446 { fn m(self) { println!("c446 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr446 { fn m(&self); }
    impl Tr446 for Box<S446> { fn m(&self) { println!("c446 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &Box<S446>) { v.m(); }
    pub fn drive() { probe(&Box::new(S446)); }
}
pub mod c447 {
    #[derive(Clone, Copy)] pub struct S447;
    pub trait Fa447 { fn m(self); }
    impl Fa447 for S447 { fn m(self) { println!("c447 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr447 { fn m(&mut self); }
    impl Tr447 for S447 { fn m(&mut self) { println!("c447 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &Box<S447>) { v.m(); }
    pub fn drive() { probe(&Box::new(S447)); }
}
pub mod c448 {
    #[derive(Clone, Copy)] pub struct S448;
    pub trait Fa448 { fn m(self); }
    impl Fa448 for S448 { fn m(self) { println!("c448 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr448 { fn m(&mut self); }
    impl Tr448 for &S448 { fn m(&mut self) { println!("c448 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &Box<S448>) { v.m(); }
    pub fn drive() { probe(&Box::new(S448)); }
}
pub mod c449 {
    #[derive(Clone, Copy)] pub struct S449;
    pub trait Fa449 { fn m(self); }
    impl Fa449 for S449 { fn m(self) { println!("c449 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr449 { fn m(&mut self); }
    impl Tr449 for &mut S449 { fn m(&mut self) { println!("c449 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &Box<S449>) { v.m(); }
    pub fn drive() { probe(&Box::new(S449)); }
}
pub mod c450 {
    #[derive(Clone, Copy)] pub struct S450;
    pub trait Fa450 { fn m(&self); }
    impl Fa450 for S450 { fn m(&self) { println!("c450 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr450 { fn m(self); }
    impl Tr450 for S450 { fn m(self) { println!("c450 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &Box<S450>) { v.m(); }
    pub fn drive() { probe(&Box::new(S450)); }
}
pub mod c451 {
    #[derive(Clone, Copy)] pub struct S451;
    pub trait Fa451 { fn m(&self); }
    impl Fa451 for S451 { fn m(&self) { println!("c451 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr451 { fn m(self); }
    impl Tr451 for &mut S451 { fn m(self) { println!("c451 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &Box<S451>) { v.m(); }
    pub fn drive() { probe(&Box::new(S451)); }
}
pub mod c452 {
    #[derive(Clone, Copy)] pub struct S452;
    pub trait Fa452 { fn m(&self); }
    impl Fa452 for S452 { fn m(&self) { println!("c452 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr452 { fn m(&self); }
    impl Tr452 for &S452 { fn m(&self) { println!("c452 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &Box<S452>) { v.m(); }
    pub fn drive() { probe(&Box::new(S452)); }
}
pub mod c453 {
    #[derive(Clone, Copy)] pub struct S453;
    pub trait Fa453 { fn m(&self); }
    impl Fa453 for S453 { fn m(&self) { println!("c453 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr453 { fn m(&self); }
    impl Tr453 for &mut S453 { fn m(&self) { println!("c453 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &Box<S453>) { v.m(); }
    pub fn drive() { probe(&Box::new(S453)); }
}
pub mod c454 {
    #[derive(Clone, Copy)] pub struct S454;
    pub trait Fa454 { fn m(&self); }
    impl Fa454 for S454 { fn m(&self) { println!("c454 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr454 { fn m(&self); }
    impl Tr454 for Box<S454> { fn m(&self) { println!("c454 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &Box<S454>) { v.m(); }
    pub fn drive() { probe(&Box::new(S454)); }
}
pub mod c455 {
    #[derive(Clone, Copy)] pub struct S455;
    pub trait Fa455 { fn m(&self); }
    impl Fa455 for S455 { fn m(&self) { println!("c455 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr455 { fn m(&mut self); }
    impl Tr455 for S455 { fn m(&mut self) { println!("c455 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &Box<S455>) { v.m(); }
    pub fn drive() { probe(&Box::new(S455)); }
}
pub mod c456 {
    #[derive(Clone, Copy)] pub struct S456;
    pub trait Fa456 { fn m(&self); }
    impl Fa456 for S456 { fn m(&self) { println!("c456 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr456 { fn m(&mut self); }
    impl Tr456 for &S456 { fn m(&mut self) { println!("c456 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &Box<S456>) { v.m(); }
    pub fn drive() { probe(&Box::new(S456)); }
}
pub mod c457 {
    #[derive(Clone, Copy)] pub struct S457;
    pub trait Fa457 { fn m(&self); }
    impl Fa457 for S457 { fn m(&self) { println!("c457 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr457 { fn m(&mut self); }
    impl Tr457 for &mut S457 { fn m(&mut self) { println!("c457 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &Box<S457>) { v.m(); }
    pub fn drive() { probe(&Box::new(S457)); }
}
pub mod c458 {
    #[derive(Clone, Copy)] pub struct S458;
    pub trait Fa458 { fn m(&mut self); }
    impl Fa458 for S458 { fn m(&mut self) { println!("c458 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr458 { fn m(self); }
    impl Tr458 for S458 { fn m(self) { println!("c458 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &Box<S458>) { v.m(); }
    pub fn drive() { probe(&Box::new(S458)); }
}
pub mod c459 {
    #[derive(Clone, Copy)] pub struct S459;
    pub trait Fa459 { fn m(&mut self); }
    impl Fa459 for S459 { fn m(&mut self) { println!("c459 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr459 { fn m(self); }
    impl Tr459 for &S459 { fn m(self) { println!("c459 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &Box<S459>) { v.m(); }
    pub fn drive() { probe(&Box::new(S459)); }
}
pub mod c460 {
    #[derive(Clone, Copy)] pub struct S460;
    pub trait Fa460 { fn m(&mut self); }
    impl Fa460 for S460 { fn m(&mut self) { println!("c460 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr460 { fn m(&self); }
    impl Tr460 for S460 { fn m(&self) { println!("c460 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &Box<S460>) { v.m(); }
    pub fn drive() { probe(&Box::new(S460)); }
}
pub mod c461 {
    #[derive(Clone, Copy)] pub struct S461;
    pub trait Fa461 { fn m(&mut self); }
    impl Fa461 for S461 { fn m(&mut self) { println!("c461 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr461 { fn m(&self); }
    impl Tr461 for Box<S461> { fn m(&self) { println!("c461 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &Box<S461>) { v.m(); }
    pub fn drive() { probe(&Box::new(S461)); }
}
pub mod c462 {
    #[derive(Clone, Copy)] pub struct S462;
    pub trait Fa462 { fn m(self); }
    impl Fa462 for &S462 { fn m(self) { println!("c462 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr462 { fn m(self); }
    impl Tr462 for S462 { fn m(self) { println!("c462 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &Box<S462>) { v.m(); }
    pub fn drive() { probe(&Box::new(S462)); }
}
pub mod c463 {
    #[derive(Clone, Copy)] pub struct S463;
    pub trait Fa463 { fn m(self); }
    impl Fa463 for &S463 { fn m(self) { println!("c463 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr463 { fn m(self); }
    impl Tr463 for &mut S463 { fn m(self) { println!("c463 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &Box<S463>) { v.m(); }
    pub fn drive() { probe(&Box::new(S463)); }
}
pub mod c464 {
    #[derive(Clone, Copy)] pub struct S464;
    pub trait Fa464 { fn m(self); }
    impl Fa464 for &S464 { fn m(self) { println!("c464 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr464 { fn m(&self); }
    impl Tr464 for &S464 { fn m(&self) { println!("c464 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &Box<S464>) { v.m(); }
    pub fn drive() { probe(&Box::new(S464)); }
}
pub mod c465 {
    #[derive(Clone, Copy)] pub struct S465;
    pub trait Fa465 { fn m(self); }
    impl Fa465 for &S465 { fn m(self) { println!("c465 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr465 { fn m(&self); }
    impl Tr465 for &mut S465 { fn m(&self) { println!("c465 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &Box<S465>) { v.m(); }
    pub fn drive() { probe(&Box::new(S465)); }
}
pub mod c466 {
    #[derive(Clone, Copy)] pub struct S466;
    pub trait Fa466 { fn m(self); }
    impl Fa466 for &S466 { fn m(self) { println!("c466 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr466 { fn m(&self); }
    impl Tr466 for Box<S466> { fn m(&self) { println!("c466 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &Box<S466>) { v.m(); }
    pub fn drive() { probe(&Box::new(S466)); }
}
pub mod c467 {
    #[derive(Clone, Copy)] pub struct S467;
    pub trait Fa467 { fn m(self); }
    impl Fa467 for &S467 { fn m(self) { println!("c467 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr467 { fn m(&mut self); }
    impl Tr467 for S467 { fn m(&mut self) { println!("c467 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &Box<S467>) { v.m(); }
    pub fn drive() { probe(&Box::new(S467)); }
}
pub mod c468 {
    #[derive(Clone, Copy)] pub struct S468;
    pub trait Fa468 { fn m(self); }
    impl Fa468 for &S468 { fn m(self) { println!("c468 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr468 { fn m(&mut self); }
    impl Tr468 for &S468 { fn m(&mut self) { println!("c468 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &Box<S468>) { v.m(); }
    pub fn drive() { probe(&Box::new(S468)); }
}
pub mod c469 {
    #[derive(Clone, Copy)] pub struct S469;
    pub trait Fa469 { fn m(self); }
    impl Fa469 for &S469 { fn m(self) { println!("c469 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr469 { fn m(&mut self); }
    impl Tr469 for &mut S469 { fn m(&mut self) { println!("c469 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &Box<S469>) { v.m(); }
    pub fn drive() { probe(&Box::new(S469)); }
}
pub mod c470 {
    #[derive(Clone, Copy)] pub struct S470;
    pub trait Fa470 { fn m(self); }
    impl Fa470 for &mut S470 { fn m(self) { println!("c470 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr470 { fn m(self); }
    impl Tr470 for S470 { fn m(self) { println!("c470 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &Box<S470>) { v.m(); }
    pub fn drive() { probe(&Box::new(S470)); }
}
pub mod c471 {
    #[derive(Clone, Copy)] pub struct S471;
    pub trait Fa471 { fn m(self); }
    impl Fa471 for &mut S471 { fn m(self) { println!("c471 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr471 { fn m(self); }
    impl Tr471 for &S471 { fn m(self) { println!("c471 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &Box<S471>) { v.m(); }
    pub fn drive() { probe(&Box::new(S471)); }
}
pub mod c472 {
    #[derive(Clone, Copy)] pub struct S472;
    pub trait Fa472 { fn m(self); }
    impl Fa472 for &mut S472 { fn m(self) { println!("c472 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr472 { fn m(&self); }
    impl Tr472 for S472 { fn m(&self) { println!("c472 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &Box<S472>) { v.m(); }
    pub fn drive() { probe(&Box::new(S472)); }
}
pub mod c473 {
    #[derive(Clone, Copy)] pub struct S473;
    pub trait Fa473 { fn m(self); }
    impl Fa473 for &mut S473 { fn m(self) { println!("c473 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr473 { fn m(&self); }
    impl Tr473 for Box<S473> { fn m(&self) { println!("c473 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &Box<S473>) { v.m(); }
    pub fn drive() { probe(&Box::new(S473)); }
}
pub mod c474 {
    #[derive(Clone, Copy)] pub struct S474;
    pub trait Tr474 { fn m(self); }
    impl Tr474 for S474 { fn m(self) { println!("c474 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut Box<S474>) { v.m(); }
    pub fn drive() { probe(&mut Box::new(S474)); }
}
pub mod c475 {
    #[derive(Clone, Copy)] pub struct S475;
    pub trait Tr475 { fn m(self); }
    impl Tr475 for &S475 { fn m(self) { println!("c475 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut Box<S475>) { v.m(); }
    pub fn drive() { probe(&mut Box::new(S475)); }
}
pub mod c476 {
    #[derive(Clone, Copy)] pub struct S476;
    pub trait Tr476 { fn m(self); }
    impl Tr476 for &mut S476 { fn m(self) { println!("c476 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut Box<S476>) { v.m(); }
    pub fn drive() { probe(&mut Box::new(S476)); }
}
pub mod c477 {
    #[derive(Clone, Copy)] pub struct S477;
    pub trait Tr477 { fn m(&self); }
    impl Tr477 for S477 { fn m(&self) { println!("c477 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut Box<S477>) { v.m(); }
    pub fn drive() { probe(&mut Box::new(S477)); }
}
pub mod c478 {
    #[derive(Clone, Copy)] pub struct S478;
    pub trait Tr478 { fn m(&self); }
    impl Tr478 for Box<S478> { fn m(&self) { println!("c478 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut Box<S478>) { v.m(); }
    pub fn drive() { probe(&mut Box::new(S478)); }
}
pub mod c479 {
    #[derive(Clone, Copy)] pub struct S479;
    pub trait Tr479 { fn m(&mut self); }
    impl Tr479 for S479 { fn m(&mut self) { println!("c479 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut Box<S479>) { v.m(); }
    pub fn drive() { probe(&mut Box::new(S479)); }
}
pub mod c480 {
    #[derive(Clone, Copy)] pub struct S480;
    pub trait Tr480 { fn m(&mut self); }
    impl Tr480 for Box<S480> { fn m(&mut self) { println!("c480 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut Box<S480>) { v.m(); }
    pub fn drive() { probe(&mut Box::new(S480)); }
}
pub mod c481 {
    #[derive(Clone, Copy)] pub struct S481;
    impl S481 { pub fn m(self) { println!("c481 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr481 { fn m(self); }
    impl Tr481 for S481 { fn m(self) { println!("c481 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut Box<S481>) { v.m(); }
    pub fn drive() { probe(&mut Box::new(S481)); }
}
pub mod c482 {
    #[derive(Clone, Copy)] pub struct S482;
    impl S482 { pub fn m(self) { println!("c482 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr482 { fn m(self); }
    impl Tr482 for &S482 { fn m(self) { println!("c482 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut Box<S482>) { v.m(); }
    pub fn drive() { probe(&mut Box::new(S482)); }
}
pub mod c483 {
    #[derive(Clone, Copy)] pub struct S483;
    impl S483 { pub fn m(self) { println!("c483 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr483 { fn m(self); }
    impl Tr483 for &mut S483 { fn m(self) { println!("c483 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut Box<S483>) { v.m(); }
    pub fn drive() { probe(&mut Box::new(S483)); }
}
pub mod c484 {
    #[derive(Clone, Copy)] pub struct S484;
    impl S484 { pub fn m(self) { println!("c484 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr484 { fn m(&self); }
    impl Tr484 for S484 { fn m(&self) { println!("c484 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut Box<S484>) { v.m(); }
    pub fn drive() { probe(&mut Box::new(S484)); }
}
pub mod c485 {
    #[derive(Clone, Copy)] pub struct S485;
    impl S485 { pub fn m(self) { println!("c485 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr485 { fn m(&self); }
    impl Tr485 for &S485 { fn m(&self) { println!("c485 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut Box<S485>) { v.m(); }
    pub fn drive() { probe(&mut Box::new(S485)); }
}
pub mod c486 {
    #[derive(Clone, Copy)] pub struct S486;
    impl S486 { pub fn m(self) { println!("c486 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr486 { fn m(&self); }
    impl Tr486 for &mut S486 { fn m(&self) { println!("c486 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut Box<S486>) { v.m(); }
    pub fn drive() { probe(&mut Box::new(S486)); }
}
pub mod c487 {
    #[derive(Clone, Copy)] pub struct S487;
    impl S487 { pub fn m(self) { println!("c487 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr487 { fn m(&self); }
    impl Tr487 for Box<S487> { fn m(&self) { println!("c487 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut Box<S487>) { v.m(); }
    pub fn drive() { probe(&mut Box::new(S487)); }
}
pub mod c488 {
    #[derive(Clone, Copy)] pub struct S488;
    impl S488 { pub fn m(self) { println!("c488 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr488 { fn m(&mut self); }
    impl Tr488 for S488 { fn m(&mut self) { println!("c488 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut Box<S488>) { v.m(); }
    pub fn drive() { probe(&mut Box::new(S488)); }
}
pub mod c489 {
    #[derive(Clone, Copy)] pub struct S489;
    impl S489 { pub fn m(self) { println!("c489 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr489 { fn m(&mut self); }
    impl Tr489 for &S489 { fn m(&mut self) { println!("c489 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut Box<S489>) { v.m(); }
    pub fn drive() { probe(&mut Box::new(S489)); }
}
pub mod c490 {
    #[derive(Clone, Copy)] pub struct S490;
    impl S490 { pub fn m(self) { println!("c490 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr490 { fn m(&mut self); }
    impl Tr490 for &mut S490 { fn m(&mut self) { println!("c490 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut Box<S490>) { v.m(); }
    pub fn drive() { probe(&mut Box::new(S490)); }
}
pub mod c491 {
    #[derive(Clone, Copy)] pub struct S491;
    impl S491 { pub fn m(self) { println!("c491 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr491 { fn m(&mut self); }
    impl Tr491 for Box<S491> { fn m(&mut self) { println!("c491 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut Box<S491>) { v.m(); }
    pub fn drive() { probe(&mut Box::new(S491)); }
}
pub mod c492 {
    #[derive(Clone, Copy)] pub struct S492;
    impl S492 { pub fn m(&self) { println!("c492 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr492 { fn m(self); }
    impl Tr492 for S492 { fn m(self) { println!("c492 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut Box<S492>) { v.m(); }
    pub fn drive() { probe(&mut Box::new(S492)); }
}
pub mod c493 {
    #[derive(Clone, Copy)] pub struct S493;
    impl S493 { pub fn m(&self) { println!("c493 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr493 { fn m(self); }
    impl Tr493 for &S493 { fn m(self) { println!("c493 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut Box<S493>) { v.m(); }
    pub fn drive() { probe(&mut Box::new(S493)); }
}
pub mod c494 {
    #[derive(Clone, Copy)] pub struct S494;
    impl S494 { pub fn m(&self) { println!("c494 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr494 { fn m(self); }
    impl Tr494 for &mut S494 { fn m(self) { println!("c494 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut Box<S494>) { v.m(); }
    pub fn drive() { probe(&mut Box::new(S494)); }
}
pub mod c495 {
    #[derive(Clone, Copy)] pub struct S495;
    impl S495 { pub fn m(&self) { println!("c495 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr495 { fn m(&self); }
    impl Tr495 for S495 { fn m(&self) { println!("c495 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut Box<S495>) { v.m(); }
    pub fn drive() { probe(&mut Box::new(S495)); }
}
pub mod c496 {
    #[derive(Clone, Copy)] pub struct S496;
    impl S496 { pub fn m(&self) { println!("c496 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr496 { fn m(&self); }
    impl Tr496 for &S496 { fn m(&self) { println!("c496 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut Box<S496>) { v.m(); }
    pub fn drive() { probe(&mut Box::new(S496)); }
}
pub mod c497 {
    #[derive(Clone, Copy)] pub struct S497;
    impl S497 { pub fn m(&self) { println!("c497 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr497 { fn m(&self); }
    impl Tr497 for &mut S497 { fn m(&self) { println!("c497 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut Box<S497>) { v.m(); }
    pub fn drive() { probe(&mut Box::new(S497)); }
}
pub mod c498 {
    #[derive(Clone, Copy)] pub struct S498;
    impl S498 { pub fn m(&self) { println!("c498 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr498 { fn m(&self); }
    impl Tr498 for Box<S498> { fn m(&self) { println!("c498 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut Box<S498>) { v.m(); }
    pub fn drive() { probe(&mut Box::new(S498)); }
}
pub mod c499 {
    #[derive(Clone, Copy)] pub struct S499;
    impl S499 { pub fn m(&self) { println!("c499 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr499 { fn m(&mut self); }
    impl Tr499 for S499 { fn m(&mut self) { println!("c499 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut Box<S499>) { v.m(); }
    pub fn drive() { probe(&mut Box::new(S499)); }
}
pub mod c500 {
    #[derive(Clone, Copy)] pub struct S500;
    impl S500 { pub fn m(&self) { println!("c500 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr500 { fn m(&mut self); }
    impl Tr500 for &S500 { fn m(&mut self) { println!("c500 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut Box<S500>) { v.m(); }
    pub fn drive() { probe(&mut Box::new(S500)); }
}
pub mod c501 {
    #[derive(Clone, Copy)] pub struct S501;
    impl S501 { pub fn m(&self) { println!("c501 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr501 { fn m(&mut self); }
    impl Tr501 for &mut S501 { fn m(&mut self) { println!("c501 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut Box<S501>) { v.m(); }
    pub fn drive() { probe(&mut Box::new(S501)); }
}
pub mod c502 {
    #[derive(Clone, Copy)] pub struct S502;
    impl S502 { pub fn m(&self) { println!("c502 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr502 { fn m(&mut self); }
    impl Tr502 for Box<S502> { fn m(&mut self) { println!("c502 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut Box<S502>) { v.m(); }
    pub fn drive() { probe(&mut Box::new(S502)); }
}
pub mod c503 {
    #[derive(Clone, Copy)] pub struct S503;
    impl S503 { pub fn m(&mut self) { println!("c503 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr503 { fn m(self); }
    impl Tr503 for S503 { fn m(self) { println!("c503 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut Box<S503>) { v.m(); }
    pub fn drive() { probe(&mut Box::new(S503)); }
}
pub mod c504 {
    #[derive(Clone, Copy)] pub struct S504;
    impl S504 { pub fn m(&mut self) { println!("c504 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr504 { fn m(self); }
    impl Tr504 for &S504 { fn m(self) { println!("c504 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut Box<S504>) { v.m(); }
    pub fn drive() { probe(&mut Box::new(S504)); }
}
pub mod c505 {
    #[derive(Clone, Copy)] pub struct S505;
    impl S505 { pub fn m(&mut self) { println!("c505 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr505 { fn m(self); }
    impl Tr505 for &mut S505 { fn m(self) { println!("c505 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut Box<S505>) { v.m(); }
    pub fn drive() { probe(&mut Box::new(S505)); }
}
pub mod c506 {
    #[derive(Clone, Copy)] pub struct S506;
    impl S506 { pub fn m(&mut self) { println!("c506 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr506 { fn m(&self); }
    impl Tr506 for S506 { fn m(&self) { println!("c506 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut Box<S506>) { v.m(); }
    pub fn drive() { probe(&mut Box::new(S506)); }
}
pub mod c507 {
    #[derive(Clone, Copy)] pub struct S507;
    impl S507 { pub fn m(&mut self) { println!("c507 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr507 { fn m(&self); }
    impl Tr507 for &S507 { fn m(&self) { println!("c507 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut Box<S507>) { v.m(); }
    pub fn drive() { probe(&mut Box::new(S507)); }
}
pub mod c508 {
    #[derive(Clone, Copy)] pub struct S508;
    impl S508 { pub fn m(&mut self) { println!("c508 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr508 { fn m(&self); }
    impl Tr508 for &mut S508 { fn m(&self) { println!("c508 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut Box<S508>) { v.m(); }
    pub fn drive() { probe(&mut Box::new(S508)); }
}
pub mod c509 {
    #[derive(Clone, Copy)] pub struct S509;
    impl S509 { pub fn m(&mut self) { println!("c509 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr509 { fn m(&self); }
    impl Tr509 for Box<S509> { fn m(&self) { println!("c509 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut Box<S509>) { v.m(); }
    pub fn drive() { probe(&mut Box::new(S509)); }
}
pub mod c510 {
    #[derive(Clone, Copy)] pub struct S510;
    impl S510 { pub fn m(&mut self) { println!("c510 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr510 { fn m(&mut self); }
    impl Tr510 for S510 { fn m(&mut self) { println!("c510 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut Box<S510>) { v.m(); }
    pub fn drive() { probe(&mut Box::new(S510)); }
}
pub mod c511 {
    #[derive(Clone, Copy)] pub struct S511;
    impl S511 { pub fn m(&mut self) { println!("c511 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr511 { fn m(&mut self); }
    impl Tr511 for &S511 { fn m(&mut self) { println!("c511 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut Box<S511>) { v.m(); }
    pub fn drive() { probe(&mut Box::new(S511)); }
}
pub mod c512 {
    #[derive(Clone, Copy)] pub struct S512;
    impl S512 { pub fn m(&mut self) { println!("c512 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr512 { fn m(&mut self); }
    impl Tr512 for &mut S512 { fn m(&mut self) { println!("c512 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut Box<S512>) { v.m(); }
    pub fn drive() { probe(&mut Box::new(S512)); }
}
pub mod c513 {
    #[derive(Clone, Copy)] pub struct S513;
    impl S513 { pub fn m(&mut self) { println!("c513 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr513 { fn m(&mut self); }
    impl Tr513 for Box<S513> { fn m(&mut self) { println!("c513 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut Box<S513>) { v.m(); }
    pub fn drive() { probe(&mut Box::new(S513)); }
}
pub mod c514 {
    #[derive(Clone, Copy)] pub struct S514;
    pub trait Fa514 { fn m(self); }
    impl Fa514 for S514 { fn m(self) { println!("c514 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr514 { fn m(self); }
    impl Tr514 for &S514 { fn m(self) { println!("c514 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut Box<S514>) { v.m(); }
    pub fn drive() { probe(&mut Box::new(S514)); }
}
pub mod c515 {
    #[derive(Clone, Copy)] pub struct S515;
    pub trait Fa515 { fn m(self); }
    impl Fa515 for S515 { fn m(self) { println!("c515 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr515 { fn m(self); }
    impl Tr515 for &mut S515 { fn m(self) { println!("c515 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut Box<S515>) { v.m(); }
    pub fn drive() { probe(&mut Box::new(S515)); }
}
pub mod c516 {
    #[derive(Clone, Copy)] pub struct S516;
    pub trait Fa516 { fn m(self); }
    impl Fa516 for S516 { fn m(self) { println!("c516 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr516 { fn m(&self); }
    impl Tr516 for S516 { fn m(&self) { println!("c516 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut Box<S516>) { v.m(); }
    pub fn drive() { probe(&mut Box::new(S516)); }
}
pub mod c517 {
    #[derive(Clone, Copy)] pub struct S517;
    pub trait Fa517 { fn m(self); }
    impl Fa517 for S517 { fn m(self) { println!("c517 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr517 { fn m(&self); }
    impl Tr517 for &S517 { fn m(&self) { println!("c517 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut Box<S517>) { v.m(); }
    pub fn drive() { probe(&mut Box::new(S517)); }
}
pub mod c518 {
    #[derive(Clone, Copy)] pub struct S518;
    pub trait Fa518 { fn m(self); }
    impl Fa518 for S518 { fn m(self) { println!("c518 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr518 { fn m(&self); }
    impl Tr518 for &mut S518 { fn m(&self) { println!("c518 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut Box<S518>) { v.m(); }
    pub fn drive() { probe(&mut Box::new(S518)); }
}
pub mod c519 {
    #[derive(Clone, Copy)] pub struct S519;
    pub trait Fa519 { fn m(self); }
    impl Fa519 for S519 { fn m(self) { println!("c519 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr519 { fn m(&self); }
    impl Tr519 for Box<S519> { fn m(&self) { println!("c519 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut Box<S519>) { v.m(); }
    pub fn drive() { probe(&mut Box::new(S519)); }
}
pub mod c520 {
    #[derive(Clone, Copy)] pub struct S520;
    pub trait Fa520 { fn m(self); }
    impl Fa520 for S520 { fn m(self) { println!("c520 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr520 { fn m(&mut self); }
    impl Tr520 for S520 { fn m(&mut self) { println!("c520 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut Box<S520>) { v.m(); }
    pub fn drive() { probe(&mut Box::new(S520)); }
}
pub mod c521 {
    #[derive(Clone, Copy)] pub struct S521;
    pub trait Fa521 { fn m(self); }
    impl Fa521 for S521 { fn m(self) { println!("c521 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr521 { fn m(&mut self); }
    impl Tr521 for &S521 { fn m(&mut self) { println!("c521 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut Box<S521>) { v.m(); }
    pub fn drive() { probe(&mut Box::new(S521)); }
}
pub mod c522 {
    #[derive(Clone, Copy)] pub struct S522;
    pub trait Fa522 { fn m(self); }
    impl Fa522 for S522 { fn m(self) { println!("c522 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr522 { fn m(&mut self); }
    impl Tr522 for &mut S522 { fn m(&mut self) { println!("c522 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut Box<S522>) { v.m(); }
    pub fn drive() { probe(&mut Box::new(S522)); }
}
pub mod c523 {
    #[derive(Clone, Copy)] pub struct S523;
    pub trait Fa523 { fn m(self); }
    impl Fa523 for S523 { fn m(self) { println!("c523 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr523 { fn m(&mut self); }
    impl Tr523 for Box<S523> { fn m(&mut self) { println!("c523 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut Box<S523>) { v.m(); }
    pub fn drive() { probe(&mut Box::new(S523)); }
}
pub mod c524 {
    #[derive(Clone, Copy)] pub struct S524;
    pub trait Fa524 { fn m(&self); }
    impl Fa524 for S524 { fn m(&self) { println!("c524 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr524 { fn m(self); }
    impl Tr524 for S524 { fn m(self) { println!("c524 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut Box<S524>) { v.m(); }
    pub fn drive() { probe(&mut Box::new(S524)); }
}
pub mod c525 {
    #[derive(Clone, Copy)] pub struct S525;
    pub trait Fa525 { fn m(&self); }
    impl Fa525 for S525 { fn m(&self) { println!("c525 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr525 { fn m(self); }
    impl Tr525 for &mut S525 { fn m(self) { println!("c525 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut Box<S525>) { v.m(); }
    pub fn drive() { probe(&mut Box::new(S525)); }
}
pub mod c526 {
    #[derive(Clone, Copy)] pub struct S526;
    pub trait Fa526 { fn m(&self); }
    impl Fa526 for S526 { fn m(&self) { println!("c526 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr526 { fn m(&self); }
    impl Tr526 for &S526 { fn m(&self) { println!("c526 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut Box<S526>) { v.m(); }
    pub fn drive() { probe(&mut Box::new(S526)); }
}
pub mod c527 {
    #[derive(Clone, Copy)] pub struct S527;
    pub trait Fa527 { fn m(&self); }
    impl Fa527 for S527 { fn m(&self) { println!("c527 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr527 { fn m(&self); }
    impl Tr527 for &mut S527 { fn m(&self) { println!("c527 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut Box<S527>) { v.m(); }
    pub fn drive() { probe(&mut Box::new(S527)); }
}
pub mod c528 {
    #[derive(Clone, Copy)] pub struct S528;
    pub trait Fa528 { fn m(&self); }
    impl Fa528 for S528 { fn m(&self) { println!("c528 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr528 { fn m(&self); }
    impl Tr528 for Box<S528> { fn m(&self) { println!("c528 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut Box<S528>) { v.m(); }
    pub fn drive() { probe(&mut Box::new(S528)); }
}
pub mod c529 {
    #[derive(Clone, Copy)] pub struct S529;
    pub trait Fa529 { fn m(&self); }
    impl Fa529 for S529 { fn m(&self) { println!("c529 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr529 { fn m(&mut self); }
    impl Tr529 for S529 { fn m(&mut self) { println!("c529 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut Box<S529>) { v.m(); }
    pub fn drive() { probe(&mut Box::new(S529)); }
}
pub mod c530 {
    #[derive(Clone, Copy)] pub struct S530;
    pub trait Fa530 { fn m(&self); }
    impl Fa530 for S530 { fn m(&self) { println!("c530 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr530 { fn m(&mut self); }
    impl Tr530 for &S530 { fn m(&mut self) { println!("c530 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut Box<S530>) { v.m(); }
    pub fn drive() { probe(&mut Box::new(S530)); }
}
pub mod c531 {
    #[derive(Clone, Copy)] pub struct S531;
    pub trait Fa531 { fn m(&self); }
    impl Fa531 for S531 { fn m(&self) { println!("c531 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr531 { fn m(&mut self); }
    impl Tr531 for &mut S531 { fn m(&mut self) { println!("c531 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut Box<S531>) { v.m(); }
    pub fn drive() { probe(&mut Box::new(S531)); }
}
pub mod c532 {
    #[derive(Clone, Copy)] pub struct S532;
    pub trait Fa532 { fn m(&self); }
    impl Fa532 for S532 { fn m(&self) { println!("c532 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr532 { fn m(&mut self); }
    impl Tr532 for Box<S532> { fn m(&mut self) { println!("c532 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut Box<S532>) { v.m(); }
    pub fn drive() { probe(&mut Box::new(S532)); }
}
pub mod c533 {
    #[derive(Clone, Copy)] pub struct S533;
    pub trait Fa533 { fn m(&mut self); }
    impl Fa533 for S533 { fn m(&mut self) { println!("c533 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr533 { fn m(self); }
    impl Tr533 for S533 { fn m(self) { println!("c533 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut Box<S533>) { v.m(); }
    pub fn drive() { probe(&mut Box::new(S533)); }
}
pub mod c534 {
    #[derive(Clone, Copy)] pub struct S534;
    pub trait Fa534 { fn m(&mut self); }
    impl Fa534 for S534 { fn m(&mut self) { println!("c534 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr534 { fn m(self); }
    impl Tr534 for &S534 { fn m(self) { println!("c534 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut Box<S534>) { v.m(); }
    pub fn drive() { probe(&mut Box::new(S534)); }
}
pub mod c535 {
    #[derive(Clone, Copy)] pub struct S535;
    pub trait Fa535 { fn m(&mut self); }
    impl Fa535 for S535 { fn m(&mut self) { println!("c535 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr535 { fn m(&self); }
    impl Tr535 for S535 { fn m(&self) { println!("c535 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut Box<S535>) { v.m(); }
    pub fn drive() { probe(&mut Box::new(S535)); }
}
pub mod c536 {
    #[derive(Clone, Copy)] pub struct S536;
    pub trait Fa536 { fn m(&mut self); }
    impl Fa536 for S536 { fn m(&mut self) { println!("c536 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr536 { fn m(&self); }
    impl Tr536 for &S536 { fn m(&self) { println!("c536 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut Box<S536>) { v.m(); }
    pub fn drive() { probe(&mut Box::new(S536)); }
}
pub mod c537 {
    #[derive(Clone, Copy)] pub struct S537;
    pub trait Fa537 { fn m(&mut self); }
    impl Fa537 for S537 { fn m(&mut self) { println!("c537 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr537 { fn m(&self); }
    impl Tr537 for &mut S537 { fn m(&self) { println!("c537 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut Box<S537>) { v.m(); }
    pub fn drive() { probe(&mut Box::new(S537)); }
}
pub mod c538 {
    #[derive(Clone, Copy)] pub struct S538;
    pub trait Fa538 { fn m(&mut self); }
    impl Fa538 for S538 { fn m(&mut self) { println!("c538 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr538 { fn m(&self); }
    impl Tr538 for Box<S538> { fn m(&self) { println!("c538 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut Box<S538>) { v.m(); }
    pub fn drive() { probe(&mut Box::new(S538)); }
}
pub mod c539 {
    #[derive(Clone, Copy)] pub struct S539;
    pub trait Fa539 { fn m(&mut self); }
    impl Fa539 for S539 { fn m(&mut self) { println!("c539 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr539 { fn m(&mut self); }
    impl Tr539 for &S539 { fn m(&mut self) { println!("c539 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut Box<S539>) { v.m(); }
    pub fn drive() { probe(&mut Box::new(S539)); }
}
pub mod c540 {
    #[derive(Clone, Copy)] pub struct S540;
    pub trait Fa540 { fn m(&mut self); }
    impl Fa540 for S540 { fn m(&mut self) { println!("c540 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr540 { fn m(&mut self); }
    impl Tr540 for &mut S540 { fn m(&mut self) { println!("c540 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut Box<S540>) { v.m(); }
    pub fn drive() { probe(&mut Box::new(S540)); }
}
pub mod c541 {
    #[derive(Clone, Copy)] pub struct S541;
    pub trait Fa541 { fn m(&mut self); }
    impl Fa541 for S541 { fn m(&mut self) { println!("c541 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr541 { fn m(&mut self); }
    impl Tr541 for Box<S541> { fn m(&mut self) { println!("c541 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut Box<S541>) { v.m(); }
    pub fn drive() { probe(&mut Box::new(S541)); }
}
pub mod c542 {
    #[derive(Clone, Copy)] pub struct S542;
    pub trait Fa542 { fn m(self); }
    impl Fa542 for &S542 { fn m(self) { println!("c542 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr542 { fn m(self); }
    impl Tr542 for S542 { fn m(self) { println!("c542 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut Box<S542>) { v.m(); }
    pub fn drive() { probe(&mut Box::new(S542)); }
}
pub mod c543 {
    #[derive(Clone, Copy)] pub struct S543;
    pub trait Fa543 { fn m(self); }
    impl Fa543 for &S543 { fn m(self) { println!("c543 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr543 { fn m(self); }
    impl Tr543 for &mut S543 { fn m(self) { println!("c543 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut Box<S543>) { v.m(); }
    pub fn drive() { probe(&mut Box::new(S543)); }
}
pub mod c544 {
    #[derive(Clone, Copy)] pub struct S544;
    pub trait Fa544 { fn m(self); }
    impl Fa544 for &S544 { fn m(self) { println!("c544 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr544 { fn m(&self); }
    impl Tr544 for &S544 { fn m(&self) { println!("c544 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut Box<S544>) { v.m(); }
    pub fn drive() { probe(&mut Box::new(S544)); }
}
pub mod c545 {
    #[derive(Clone, Copy)] pub struct S545;
    pub trait Fa545 { fn m(self); }
    impl Fa545 for &S545 { fn m(self) { println!("c545 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr545 { fn m(&self); }
    impl Tr545 for &mut S545 { fn m(&self) { println!("c545 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut Box<S545>) { v.m(); }
    pub fn drive() { probe(&mut Box::new(S545)); }
}
pub mod c546 {
    #[derive(Clone, Copy)] pub struct S546;
    pub trait Fa546 { fn m(self); }
    impl Fa546 for &S546 { fn m(self) { println!("c546 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr546 { fn m(&self); }
    impl Tr546 for Box<S546> { fn m(&self) { println!("c546 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut Box<S546>) { v.m(); }
    pub fn drive() { probe(&mut Box::new(S546)); }
}
pub mod c547 {
    #[derive(Clone, Copy)] pub struct S547;
    pub trait Fa547 { fn m(self); }
    impl Fa547 for &S547 { fn m(self) { println!("c547 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr547 { fn m(&mut self); }
    impl Tr547 for S547 { fn m(&mut self) { println!("c547 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut Box<S547>) { v.m(); }
    pub fn drive() { probe(&mut Box::new(S547)); }
}
pub mod c548 {
    #[derive(Clone, Copy)] pub struct S548;
    pub trait Fa548 { fn m(self); }
    impl Fa548 for &S548 { fn m(self) { println!("c548 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr548 { fn m(&mut self); }
    impl Tr548 for &S548 { fn m(&mut self) { println!("c548 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut Box<S548>) { v.m(); }
    pub fn drive() { probe(&mut Box::new(S548)); }
}
pub mod c549 {
    #[derive(Clone, Copy)] pub struct S549;
    pub trait Fa549 { fn m(self); }
    impl Fa549 for &S549 { fn m(self) { println!("c549 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr549 { fn m(&mut self); }
    impl Tr549 for &mut S549 { fn m(&mut self) { println!("c549 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut Box<S549>) { v.m(); }
    pub fn drive() { probe(&mut Box::new(S549)); }
}
pub mod c550 {
    #[derive(Clone, Copy)] pub struct S550;
    pub trait Fa550 { fn m(self); }
    impl Fa550 for &S550 { fn m(self) { println!("c550 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr550 { fn m(&mut self); }
    impl Tr550 for Box<S550> { fn m(&mut self) { println!("c550 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut Box<S550>) { v.m(); }
    pub fn drive() { probe(&mut Box::new(S550)); }
}
pub mod c551 {
    #[derive(Clone, Copy)] pub struct S551;
    pub trait Fa551 { fn m(self); }
    impl Fa551 for &mut S551 { fn m(self) { println!("c551 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr551 { fn m(self); }
    impl Tr551 for S551 { fn m(self) { println!("c551 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut Box<S551>) { v.m(); }
    pub fn drive() { probe(&mut Box::new(S551)); }
}
pub mod c552 {
    #[derive(Clone, Copy)] pub struct S552;
    pub trait Fa552 { fn m(self); }
    impl Fa552 for &mut S552 { fn m(self) { println!("c552 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr552 { fn m(self); }
    impl Tr552 for &S552 { fn m(self) { println!("c552 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut Box<S552>) { v.m(); }
    pub fn drive() { probe(&mut Box::new(S552)); }
}
pub mod c553 {
    #[derive(Clone, Copy)] pub struct S553;
    pub trait Fa553 { fn m(self); }
    impl Fa553 for &mut S553 { fn m(self) { println!("c553 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr553 { fn m(&self); }
    impl Tr553 for S553 { fn m(&self) { println!("c553 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut Box<S553>) { v.m(); }
    pub fn drive() { probe(&mut Box::new(S553)); }
}
pub mod c554 {
    #[derive(Clone, Copy)] pub struct S554;
    pub trait Fa554 { fn m(self); }
    impl Fa554 for &mut S554 { fn m(self) { println!("c554 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr554 { fn m(&self); }
    impl Tr554 for &S554 { fn m(&self) { println!("c554 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut Box<S554>) { v.m(); }
    pub fn drive() { probe(&mut Box::new(S554)); }
}
pub mod c555 {
    #[derive(Clone, Copy)] pub struct S555;
    pub trait Fa555 { fn m(self); }
    impl Fa555 for &mut S555 { fn m(self) { println!("c555 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr555 { fn m(&self); }
    impl Tr555 for &mut S555 { fn m(&self) { println!("c555 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut Box<S555>) { v.m(); }
    pub fn drive() { probe(&mut Box::new(S555)); }
}
pub mod c556 {
    #[derive(Clone, Copy)] pub struct S556;
    pub trait Fa556 { fn m(self); }
    impl Fa556 for &mut S556 { fn m(self) { println!("c556 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr556 { fn m(&self); }
    impl Tr556 for Box<S556> { fn m(&self) { println!("c556 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut Box<S556>) { v.m(); }
    pub fn drive() { probe(&mut Box::new(S556)); }
}
pub mod c557 {
    #[derive(Clone, Copy)] pub struct S557;
    pub trait Fa557 { fn m(self); }
    impl Fa557 for &mut S557 { fn m(self) { println!("c557 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr557 { fn m(&mut self); }
    impl Tr557 for &S557 { fn m(&mut self) { println!("c557 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut Box<S557>) { v.m(); }
    pub fn drive() { probe(&mut Box::new(S557)); }
}
pub mod c558 {
    #[derive(Clone, Copy)] pub struct S558;
    pub trait Fa558 { fn m(self); }
    impl Fa558 for &mut S558 { fn m(self) { println!("c558 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr558 { fn m(&mut self); }
    impl Tr558 for &mut S558 { fn m(&mut self) { println!("c558 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut Box<S558>) { v.m(); }
    pub fn drive() { probe(&mut Box::new(S558)); }
}
pub mod c559 {
    #[derive(Clone, Copy)] pub struct S559;
    pub trait Fa559 { fn m(self); }
    impl Fa559 for &mut S559 { fn m(self) { println!("c559 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr559 { fn m(&mut self); }
    impl Tr559 for Box<S559> { fn m(&mut self) { println!("c559 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: &mut Box<S559>) { v.m(); }
    pub fn drive() { probe(&mut Box::new(S559)); }
}
pub mod c560 {
    #[derive(Clone, Copy)] pub struct S560;
    pub trait Tr560 { fn m(self); }
    impl Tr560 for S560 { fn m(self) { println!("c560 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: std::rc::Rc<S560>) { v.m(); }
    pub fn drive() { probe(std::rc::Rc::new(S560)); }
}
pub mod c561 {
    #[derive(Clone, Copy)] pub struct S561;
    pub trait Tr561 { fn m(self); }
    impl Tr561 for &S561 { fn m(self) { println!("c561 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: std::rc::Rc<S561>) { v.m(); }
    pub fn drive() { probe(std::rc::Rc::new(S561)); }
}
pub mod c562 {
    #[derive(Clone, Copy)] pub struct S562;
    pub trait Tr562 { fn m(&self); }
    impl Tr562 for S562 { fn m(&self) { println!("c562 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: std::rc::Rc<S562>) { v.m(); }
    pub fn drive() { probe(std::rc::Rc::new(S562)); }
}
pub mod c563 {
    #[derive(Clone, Copy)] pub struct S563;
    impl S563 { pub fn m(self) { println!("c563 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr563 { fn m(self); }
    impl Tr563 for S563 { fn m(self) { println!("c563 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: std::rc::Rc<S563>) { v.m(); }
    pub fn drive() { probe(std::rc::Rc::new(S563)); }
}
pub mod c564 {
    #[derive(Clone, Copy)] pub struct S564;
    impl S564 { pub fn m(self) { println!("c564 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr564 { fn m(self); }
    impl Tr564 for &S564 { fn m(self) { println!("c564 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: std::rc::Rc<S564>) { v.m(); }
    pub fn drive() { probe(std::rc::Rc::new(S564)); }
}
pub mod c565 {
    #[derive(Clone, Copy)] pub struct S565;
    impl S565 { pub fn m(self) { println!("c565 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr565 { fn m(self); }
    impl Tr565 for &mut S565 { fn m(self) { println!("c565 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: std::rc::Rc<S565>) { v.m(); }
    pub fn drive() { probe(std::rc::Rc::new(S565)); }
}
pub mod c566 {
    #[derive(Clone, Copy)] pub struct S566;
    impl S566 { pub fn m(self) { println!("c566 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr566 { fn m(self); }
    impl Tr566 for Box<S566> { fn m(self) { println!("c566 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: std::rc::Rc<S566>) { v.m(); }
    pub fn drive() { probe(std::rc::Rc::new(S566)); }
}
pub mod c567 {
    #[derive(Clone, Copy)] pub struct S567;
    impl S567 { pub fn m(self) { println!("c567 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr567 { fn m(&self); }
    impl Tr567 for S567 { fn m(&self) { println!("c567 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: std::rc::Rc<S567>) { v.m(); }
    pub fn drive() { probe(std::rc::Rc::new(S567)); }
}
pub mod c568 {
    #[derive(Clone, Copy)] pub struct S568;
    impl S568 { pub fn m(self) { println!("c568 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr568 { fn m(&self); }
    impl Tr568 for &S568 { fn m(&self) { println!("c568 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: std::rc::Rc<S568>) { v.m(); }
    pub fn drive() { probe(std::rc::Rc::new(S568)); }
}
pub mod c569 {
    #[derive(Clone, Copy)] pub struct S569;
    impl S569 { pub fn m(self) { println!("c569 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr569 { fn m(&self); }
    impl Tr569 for &mut S569 { fn m(&self) { println!("c569 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: std::rc::Rc<S569>) { v.m(); }
    pub fn drive() { probe(std::rc::Rc::new(S569)); }
}
pub mod c570 {
    #[derive(Clone, Copy)] pub struct S570;
    impl S570 { pub fn m(self) { println!("c570 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr570 { fn m(&self); }
    impl Tr570 for Box<S570> { fn m(&self) { println!("c570 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: std::rc::Rc<S570>) { v.m(); }
    pub fn drive() { probe(std::rc::Rc::new(S570)); }
}
pub mod c571 {
    #[derive(Clone, Copy)] pub struct S571;
    impl S571 { pub fn m(self) { println!("c571 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr571 { fn m(&mut self); }
    impl Tr571 for S571 { fn m(&mut self) { println!("c571 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: std::rc::Rc<S571>) { v.m(); }
    pub fn drive() { probe(std::rc::Rc::new(S571)); }
}
pub mod c572 {
    #[derive(Clone, Copy)] pub struct S572;
    impl S572 { pub fn m(self) { println!("c572 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr572 { fn m(&mut self); }
    impl Tr572 for &S572 { fn m(&mut self) { println!("c572 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: std::rc::Rc<S572>) { v.m(); }
    pub fn drive() { probe(std::rc::Rc::new(S572)); }
}
pub mod c573 {
    #[derive(Clone, Copy)] pub struct S573;
    impl S573 { pub fn m(self) { println!("c573 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr573 { fn m(&mut self); }
    impl Tr573 for &mut S573 { fn m(&mut self) { println!("c573 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: std::rc::Rc<S573>) { v.m(); }
    pub fn drive() { probe(std::rc::Rc::new(S573)); }
}
pub mod c574 {
    #[derive(Clone, Copy)] pub struct S574;
    impl S574 { pub fn m(self) { println!("c574 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr574 { fn m(&mut self); }
    impl Tr574 for Box<S574> { fn m(&mut self) { println!("c574 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: std::rc::Rc<S574>) { v.m(); }
    pub fn drive() { probe(std::rc::Rc::new(S574)); }
}
pub mod c575 {
    #[derive(Clone, Copy)] pub struct S575;
    impl S575 { pub fn m(&self) { println!("c575 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr575 { fn m(self); }
    impl Tr575 for S575 { fn m(self) { println!("c575 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: std::rc::Rc<S575>) { v.m(); }
    pub fn drive() { probe(std::rc::Rc::new(S575)); }
}
pub mod c576 {
    #[derive(Clone, Copy)] pub struct S576;
    impl S576 { pub fn m(&self) { println!("c576 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr576 { fn m(self); }
    impl Tr576 for &S576 { fn m(self) { println!("c576 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: std::rc::Rc<S576>) { v.m(); }
    pub fn drive() { probe(std::rc::Rc::new(S576)); }
}
pub mod c577 {
    #[derive(Clone, Copy)] pub struct S577;
    impl S577 { pub fn m(&self) { println!("c577 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr577 { fn m(self); }
    impl Tr577 for &mut S577 { fn m(self) { println!("c577 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: std::rc::Rc<S577>) { v.m(); }
    pub fn drive() { probe(std::rc::Rc::new(S577)); }
}
pub mod c578 {
    #[derive(Clone, Copy)] pub struct S578;
    impl S578 { pub fn m(&self) { println!("c578 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr578 { fn m(self); }
    impl Tr578 for Box<S578> { fn m(self) { println!("c578 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: std::rc::Rc<S578>) { v.m(); }
    pub fn drive() { probe(std::rc::Rc::new(S578)); }
}
pub mod c579 {
    #[derive(Clone, Copy)] pub struct S579;
    impl S579 { pub fn m(&self) { println!("c579 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr579 { fn m(&self); }
    impl Tr579 for S579 { fn m(&self) { println!("c579 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: std::rc::Rc<S579>) { v.m(); }
    pub fn drive() { probe(std::rc::Rc::new(S579)); }
}
pub mod c580 {
    #[derive(Clone, Copy)] pub struct S580;
    impl S580 { pub fn m(&self) { println!("c580 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr580 { fn m(&self); }
    impl Tr580 for &S580 { fn m(&self) { println!("c580 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: std::rc::Rc<S580>) { v.m(); }
    pub fn drive() { probe(std::rc::Rc::new(S580)); }
}
pub mod c581 {
    #[derive(Clone, Copy)] pub struct S581;
    impl S581 { pub fn m(&self) { println!("c581 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr581 { fn m(&self); }
    impl Tr581 for &mut S581 { fn m(&self) { println!("c581 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: std::rc::Rc<S581>) { v.m(); }
    pub fn drive() { probe(std::rc::Rc::new(S581)); }
}
pub mod c582 {
    #[derive(Clone, Copy)] pub struct S582;
    impl S582 { pub fn m(&self) { println!("c582 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr582 { fn m(&self); }
    impl Tr582 for Box<S582> { fn m(&self) { println!("c582 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: std::rc::Rc<S582>) { v.m(); }
    pub fn drive() { probe(std::rc::Rc::new(S582)); }
}
pub mod c583 {
    #[derive(Clone, Copy)] pub struct S583;
    impl S583 { pub fn m(&self) { println!("c583 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr583 { fn m(&mut self); }
    impl Tr583 for S583 { fn m(&mut self) { println!("c583 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: std::rc::Rc<S583>) { v.m(); }
    pub fn drive() { probe(std::rc::Rc::new(S583)); }
}
pub mod c584 {
    #[derive(Clone, Copy)] pub struct S584;
    impl S584 { pub fn m(&self) { println!("c584 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr584 { fn m(&mut self); }
    impl Tr584 for &S584 { fn m(&mut self) { println!("c584 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: std::rc::Rc<S584>) { v.m(); }
    pub fn drive() { probe(std::rc::Rc::new(S584)); }
}
pub mod c585 {
    #[derive(Clone, Copy)] pub struct S585;
    impl S585 { pub fn m(&self) { println!("c585 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr585 { fn m(&mut self); }
    impl Tr585 for &mut S585 { fn m(&mut self) { println!("c585 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: std::rc::Rc<S585>) { v.m(); }
    pub fn drive() { probe(std::rc::Rc::new(S585)); }
}
pub mod c586 {
    #[derive(Clone, Copy)] pub struct S586;
    impl S586 { pub fn m(&self) { println!("c586 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr586 { fn m(&mut self); }
    impl Tr586 for Box<S586> { fn m(&mut self) { println!("c586 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: std::rc::Rc<S586>) { v.m(); }
    pub fn drive() { probe(std::rc::Rc::new(S586)); }
}
pub mod c587 {
    #[derive(Clone, Copy)] pub struct S587;
    impl S587 { pub fn m(&mut self) { println!("c587 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr587 { fn m(self); }
    impl Tr587 for S587 { fn m(self) { println!("c587 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: std::rc::Rc<S587>) { v.m(); }
    pub fn drive() { probe(std::rc::Rc::new(S587)); }
}
pub mod c588 {
    #[derive(Clone, Copy)] pub struct S588;
    impl S588 { pub fn m(&mut self) { println!("c588 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr588 { fn m(self); }
    impl Tr588 for &S588 { fn m(self) { println!("c588 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: std::rc::Rc<S588>) { v.m(); }
    pub fn drive() { probe(std::rc::Rc::new(S588)); }
}
pub mod c589 {
    #[derive(Clone, Copy)] pub struct S589;
    impl S589 { pub fn m(&mut self) { println!("c589 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr589 { fn m(&self); }
    impl Tr589 for S589 { fn m(&self) { println!("c589 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: std::rc::Rc<S589>) { v.m(); }
    pub fn drive() { probe(std::rc::Rc::new(S589)); }
}
pub mod c590 {
    #[derive(Clone, Copy)] pub struct S590;
    pub trait Fa590 { fn m(self); }
    impl Fa590 for S590 { fn m(self) { println!("c590 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr590 { fn m(self); }
    impl Tr590 for &S590 { fn m(self) { println!("c590 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: std::rc::Rc<S590>) { v.m(); }
    pub fn drive() { probe(std::rc::Rc::new(S590)); }
}
pub mod c591 {
    #[derive(Clone, Copy)] pub struct S591;
    pub trait Fa591 { fn m(self); }
    impl Fa591 for S591 { fn m(self) { println!("c591 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr591 { fn m(self); }
    impl Tr591 for &mut S591 { fn m(self) { println!("c591 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: std::rc::Rc<S591>) { v.m(); }
    pub fn drive() { probe(std::rc::Rc::new(S591)); }
}
pub mod c592 {
    #[derive(Clone, Copy)] pub struct S592;
    pub trait Fa592 { fn m(self); }
    impl Fa592 for S592 { fn m(self) { println!("c592 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr592 { fn m(self); }
    impl Tr592 for Box<S592> { fn m(self) { println!("c592 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: std::rc::Rc<S592>) { v.m(); }
    pub fn drive() { probe(std::rc::Rc::new(S592)); }
}
pub mod c593 {
    #[derive(Clone, Copy)] pub struct S593;
    pub trait Fa593 { fn m(self); }
    impl Fa593 for S593 { fn m(self) { println!("c593 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr593 { fn m(&self); }
    impl Tr593 for S593 { fn m(&self) { println!("c593 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: std::rc::Rc<S593>) { v.m(); }
    pub fn drive() { probe(std::rc::Rc::new(S593)); }
}
pub mod c594 {
    #[derive(Clone, Copy)] pub struct S594;
    pub trait Fa594 { fn m(self); }
    impl Fa594 for S594 { fn m(self) { println!("c594 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr594 { fn m(&self); }
    impl Tr594 for &S594 { fn m(&self) { println!("c594 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: std::rc::Rc<S594>) { v.m(); }
    pub fn drive() { probe(std::rc::Rc::new(S594)); }
}
pub mod c595 {
    #[derive(Clone, Copy)] pub struct S595;
    pub trait Fa595 { fn m(self); }
    impl Fa595 for S595 { fn m(self) { println!("c595 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr595 { fn m(&self); }
    impl Tr595 for &mut S595 { fn m(&self) { println!("c595 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: std::rc::Rc<S595>) { v.m(); }
    pub fn drive() { probe(std::rc::Rc::new(S595)); }
}
pub mod c596 {
    #[derive(Clone, Copy)] pub struct S596;
    pub trait Fa596 { fn m(self); }
    impl Fa596 for S596 { fn m(self) { println!("c596 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr596 { fn m(&self); }
    impl Tr596 for Box<S596> { fn m(&self) { println!("c596 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: std::rc::Rc<S596>) { v.m(); }
    pub fn drive() { probe(std::rc::Rc::new(S596)); }
}
pub mod c597 {
    #[derive(Clone, Copy)] pub struct S597;
    pub trait Fa597 { fn m(self); }
    impl Fa597 for S597 { fn m(self) { println!("c597 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr597 { fn m(&mut self); }
    impl Tr597 for S597 { fn m(&mut self) { println!("c597 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: std::rc::Rc<S597>) { v.m(); }
    pub fn drive() { probe(std::rc::Rc::new(S597)); }
}
pub mod c598 {
    #[derive(Clone, Copy)] pub struct S598;
    pub trait Fa598 { fn m(self); }
    impl Fa598 for S598 { fn m(self) { println!("c598 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr598 { fn m(&mut self); }
    impl Tr598 for &S598 { fn m(&mut self) { println!("c598 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: std::rc::Rc<S598>) { v.m(); }
    pub fn drive() { probe(std::rc::Rc::new(S598)); }
}
pub mod c599 {
    #[derive(Clone, Copy)] pub struct S599;
    pub trait Fa599 { fn m(self); }
    impl Fa599 for S599 { fn m(self) { println!("c599 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr599 { fn m(&mut self); }
    impl Tr599 for &mut S599 { fn m(&mut self) { println!("c599 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: std::rc::Rc<S599>) { v.m(); }
    pub fn drive() { probe(std::rc::Rc::new(S599)); }
}
pub mod c600 {
    #[derive(Clone, Copy)] pub struct S600;
    pub trait Fa600 { fn m(self); }
    impl Fa600 for S600 { fn m(self) { println!("c600 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr600 { fn m(&mut self); }
    impl Tr600 for Box<S600> { fn m(&mut self) { println!("c600 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: std::rc::Rc<S600>) { v.m(); }
    pub fn drive() { probe(std::rc::Rc::new(S600)); }
}
pub mod c601 {
    #[derive(Clone, Copy)] pub struct S601;
    pub trait Fa601 { fn m(&self); }
    impl Fa601 for S601 { fn m(&self) { println!("c601 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr601 { fn m(self); }
    impl Tr601 for S601 { fn m(self) { println!("c601 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: std::rc::Rc<S601>) { v.m(); }
    pub fn drive() { probe(std::rc::Rc::new(S601)); }
}
pub mod c602 {
    #[derive(Clone, Copy)] pub struct S602;
    pub trait Fa602 { fn m(&self); }
    impl Fa602 for S602 { fn m(&self) { println!("c602 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr602 { fn m(self); }
    impl Tr602 for &mut S602 { fn m(self) { println!("c602 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: std::rc::Rc<S602>) { v.m(); }
    pub fn drive() { probe(std::rc::Rc::new(S602)); }
}
pub mod c603 {
    #[derive(Clone, Copy)] pub struct S603;
    pub trait Fa603 { fn m(&self); }
    impl Fa603 for S603 { fn m(&self) { println!("c603 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr603 { fn m(self); }
    impl Tr603 for Box<S603> { fn m(self) { println!("c603 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: std::rc::Rc<S603>) { v.m(); }
    pub fn drive() { probe(std::rc::Rc::new(S603)); }
}
pub mod c604 {
    #[derive(Clone, Copy)] pub struct S604;
    pub trait Fa604 { fn m(&self); }
    impl Fa604 for S604 { fn m(&self) { println!("c604 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr604 { fn m(&self); }
    impl Tr604 for &S604 { fn m(&self) { println!("c604 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: std::rc::Rc<S604>) { v.m(); }
    pub fn drive() { probe(std::rc::Rc::new(S604)); }
}
pub mod c605 {
    #[derive(Clone, Copy)] pub struct S605;
    pub trait Fa605 { fn m(&self); }
    impl Fa605 for S605 { fn m(&self) { println!("c605 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr605 { fn m(&self); }
    impl Tr605 for &mut S605 { fn m(&self) { println!("c605 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: std::rc::Rc<S605>) { v.m(); }
    pub fn drive() { probe(std::rc::Rc::new(S605)); }
}
pub mod c606 {
    #[derive(Clone, Copy)] pub struct S606;
    pub trait Fa606 { fn m(&self); }
    impl Fa606 for S606 { fn m(&self) { println!("c606 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr606 { fn m(&self); }
    impl Tr606 for Box<S606> { fn m(&self) { println!("c606 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: std::rc::Rc<S606>) { v.m(); }
    pub fn drive() { probe(std::rc::Rc::new(S606)); }
}
pub mod c607 {
    #[derive(Clone, Copy)] pub struct S607;
    pub trait Fa607 { fn m(&self); }
    impl Fa607 for S607 { fn m(&self) { println!("c607 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr607 { fn m(&mut self); }
    impl Tr607 for S607 { fn m(&mut self) { println!("c607 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: std::rc::Rc<S607>) { v.m(); }
    pub fn drive() { probe(std::rc::Rc::new(S607)); }
}
pub mod c608 {
    #[derive(Clone, Copy)] pub struct S608;
    pub trait Fa608 { fn m(&self); }
    impl Fa608 for S608 { fn m(&self) { println!("c608 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr608 { fn m(&mut self); }
    impl Tr608 for &S608 { fn m(&mut self) { println!("c608 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: std::rc::Rc<S608>) { v.m(); }
    pub fn drive() { probe(std::rc::Rc::new(S608)); }
}
pub mod c609 {
    #[derive(Clone, Copy)] pub struct S609;
    pub trait Fa609 { fn m(&self); }
    impl Fa609 for S609 { fn m(&self) { println!("c609 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr609 { fn m(&mut self); }
    impl Tr609 for &mut S609 { fn m(&mut self) { println!("c609 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: std::rc::Rc<S609>) { v.m(); }
    pub fn drive() { probe(std::rc::Rc::new(S609)); }
}
pub mod c610 {
    #[derive(Clone, Copy)] pub struct S610;
    pub trait Fa610 { fn m(&self); }
    impl Fa610 for S610 { fn m(&self) { println!("c610 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr610 { fn m(&mut self); }
    impl Tr610 for Box<S610> { fn m(&mut self) { println!("c610 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: std::rc::Rc<S610>) { v.m(); }
    pub fn drive() { probe(std::rc::Rc::new(S610)); }
}
pub mod c611 {
    #[derive(Clone, Copy)] pub struct S611;
    pub trait Fa611 { fn m(&mut self); }
    impl Fa611 for S611 { fn m(&mut self) { println!("c611 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr611 { fn m(self); }
    impl Tr611 for S611 { fn m(self) { println!("c611 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: std::rc::Rc<S611>) { v.m(); }
    pub fn drive() { probe(std::rc::Rc::new(S611)); }
}
pub mod c612 {
    #[derive(Clone, Copy)] pub struct S612;
    pub trait Fa612 { fn m(&mut self); }
    impl Fa612 for S612 { fn m(&mut self) { println!("c612 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr612 { fn m(self); }
    impl Tr612 for &S612 { fn m(self) { println!("c612 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: std::rc::Rc<S612>) { v.m(); }
    pub fn drive() { probe(std::rc::Rc::new(S612)); }
}
pub mod c613 {
    #[derive(Clone, Copy)] pub struct S613;
    pub trait Fa613 { fn m(&mut self); }
    impl Fa613 for S613 { fn m(&mut self) { println!("c613 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr613 { fn m(&self); }
    impl Tr613 for S613 { fn m(&self) { println!("c613 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: std::rc::Rc<S613>) { v.m(); }
    pub fn drive() { probe(std::rc::Rc::new(S613)); }
}
pub mod c614 {
    #[derive(Clone, Copy)] pub struct S614;
    pub trait Fa614 { fn m(self); }
    impl Fa614 for &S614 { fn m(self) { println!("c614 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr614 { fn m(self); }
    impl Tr614 for S614 { fn m(self) { println!("c614 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: std::rc::Rc<S614>) { v.m(); }
    pub fn drive() { probe(std::rc::Rc::new(S614)); }
}
pub mod c615 {
    #[derive(Clone, Copy)] pub struct S615;
    pub trait Fa615 { fn m(self); }
    impl Fa615 for &S615 { fn m(self) { println!("c615 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr615 { fn m(self); }
    impl Tr615 for &mut S615 { fn m(self) { println!("c615 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: std::rc::Rc<S615>) { v.m(); }
    pub fn drive() { probe(std::rc::Rc::new(S615)); }
}
pub mod c616 {
    #[derive(Clone, Copy)] pub struct S616;
    pub trait Fa616 { fn m(self); }
    impl Fa616 for &S616 { fn m(self) { println!("c616 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr616 { fn m(self); }
    impl Tr616 for Box<S616> { fn m(self) { println!("c616 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: std::rc::Rc<S616>) { v.m(); }
    pub fn drive() { probe(std::rc::Rc::new(S616)); }
}
pub mod c617 {
    #[derive(Clone, Copy)] pub struct S617;
    pub trait Fa617 { fn m(self); }
    impl Fa617 for &S617 { fn m(self) { println!("c617 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr617 { fn m(&self); }
    impl Tr617 for &S617 { fn m(&self) { println!("c617 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: std::rc::Rc<S617>) { v.m(); }
    pub fn drive() { probe(std::rc::Rc::new(S617)); }
}
pub mod c618 {
    #[derive(Clone, Copy)] pub struct S618;
    pub trait Fa618 { fn m(self); }
    impl Fa618 for &S618 { fn m(self) { println!("c618 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr618 { fn m(&self); }
    impl Tr618 for &mut S618 { fn m(&self) { println!("c618 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: std::rc::Rc<S618>) { v.m(); }
    pub fn drive() { probe(std::rc::Rc::new(S618)); }
}
pub mod c619 {
    #[derive(Clone, Copy)] pub struct S619;
    pub trait Fa619 { fn m(self); }
    impl Fa619 for &S619 { fn m(self) { println!("c619 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr619 { fn m(&self); }
    impl Tr619 for Box<S619> { fn m(&self) { println!("c619 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: std::rc::Rc<S619>) { v.m(); }
    pub fn drive() { probe(std::rc::Rc::new(S619)); }
}
pub mod c620 {
    #[derive(Clone, Copy)] pub struct S620;
    pub trait Fa620 { fn m(self); }
    impl Fa620 for &S620 { fn m(self) { println!("c620 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr620 { fn m(&mut self); }
    impl Tr620 for S620 { fn m(&mut self) { println!("c620 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: std::rc::Rc<S620>) { v.m(); }
    pub fn drive() { probe(std::rc::Rc::new(S620)); }
}
pub mod c621 {
    #[derive(Clone, Copy)] pub struct S621;
    pub trait Fa621 { fn m(self); }
    impl Fa621 for &S621 { fn m(self) { println!("c621 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr621 { fn m(&mut self); }
    impl Tr621 for &S621 { fn m(&mut self) { println!("c621 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: std::rc::Rc<S621>) { v.m(); }
    pub fn drive() { probe(std::rc::Rc::new(S621)); }
}
pub mod c622 {
    #[derive(Clone, Copy)] pub struct S622;
    pub trait Fa622 { fn m(self); }
    impl Fa622 for &S622 { fn m(self) { println!("c622 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr622 { fn m(&mut self); }
    impl Tr622 for &mut S622 { fn m(&mut self) { println!("c622 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: std::rc::Rc<S622>) { v.m(); }
    pub fn drive() { probe(std::rc::Rc::new(S622)); }
}
pub mod c623 {
    #[derive(Clone, Copy)] pub struct S623;
    pub trait Fa623 { fn m(self); }
    impl Fa623 for &S623 { fn m(self) { println!("c623 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr623 { fn m(&mut self); }
    impl Tr623 for Box<S623> { fn m(&mut self) { println!("c623 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: std::rc::Rc<S623>) { v.m(); }
    pub fn drive() { probe(std::rc::Rc::new(S623)); }
}
pub mod c624 {
    #[derive(Clone, Copy)] pub struct S624;
    pub trait Fa624 { fn m(self); }
    impl Fa624 for &mut S624 { fn m(self) { println!("c624 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr624 { fn m(self); }
    impl Tr624 for S624 { fn m(self) { println!("c624 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: std::rc::Rc<S624>) { v.m(); }
    pub fn drive() { probe(std::rc::Rc::new(S624)); }
}
pub mod c625 {
    #[derive(Clone, Copy)] pub struct S625;
    pub trait Fa625 { fn m(self); }
    impl Fa625 for &mut S625 { fn m(self) { println!("c625 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr625 { fn m(self); }
    impl Tr625 for &S625 { fn m(self) { println!("c625 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: std::rc::Rc<S625>) { v.m(); }
    pub fn drive() { probe(std::rc::Rc::new(S625)); }
}
pub mod c626 {
    #[derive(Clone, Copy)] pub struct S626;
    pub trait Fa626 { fn m(self); }
    impl Fa626 for &mut S626 { fn m(self) { println!("c626 first\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub trait Tr626 { fn m(&self); }
    impl Tr626 for S626 { fn m(&self) { println!("c626 tr\n{}", std::backtrace::Backtrace::force_capture()); } }
    pub fn probe(v: std::rc::Rc<S626>) { v.m(); }
    pub fn drive() { probe(std::rc::Rc::new(S626)); }
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
    c255::drive();
    c256::drive();
    c257::drive();
    c258::drive();
    c259::drive();
    c260::drive();
    c261::drive();
    c262::drive();
    c263::drive();
    c264::drive();
    c265::drive();
    c266::drive();
    c267::drive();
    c268::drive();
    c269::drive();
    c270::drive();
    c271::drive();
    c272::drive();
    c273::drive();
    c274::drive();
    c275::drive();
    c276::drive();
    c277::drive();
    c278::drive();
    c279::drive();
    c280::drive();
    c281::drive();
    c282::drive();
    c283::drive();
    c284::drive();
    c285::drive();
    c286::drive();
    c287::drive();
    c288::drive();
    c289::drive();
    c290::drive();
    c291::drive();
    c292::drive();
    c293::drive();
    c294::drive();
    c295::drive();
    c296::drive();
    c297::drive();
    c298::drive();
    c299::drive();
    c300::drive();
    c301::drive();
    c302::drive();
    c303::drive();
    c304::drive();
    c305::drive();
    c306::drive();
    c307::drive();
    c308::drive();
    c309::drive();
    c310::drive();
    c311::drive();
    c312::drive();
    c313::drive();
    c314::drive();
    c315::drive();
    c316::drive();
    c317::drive();
    c318::drive();
    c319::drive();
    c320::drive();
    c321::drive();
    c322::drive();
    c323::drive();
    c324::drive();
    c325::drive();
    c326::drive();
    c327::drive();
    c328::drive();
    c329::drive();
    c330::drive();
    c331::drive();
    c332::drive();
    c333::drive();
    c334::drive();
    c335::drive();
    c336::drive();
    c337::drive();
    c338::drive();
    c339::drive();
    c340::drive();
    c341::drive();
    c342::drive();
    c343::drive();
    c344::drive();
    c345::drive();
    c346::drive();
    c347::drive();
    c348::drive();
    c349::drive();
    c350::drive();
    c351::drive();
    c352::drive();
    c353::drive();
    c354::drive();
    c355::drive();
    c356::drive();
    c357::drive();
    c358::drive();
    c359::drive();
    c360::drive();
    c361::drive();
    c362::drive();
    c363::drive();
    c364::drive();
    c365::drive();
    c366::drive();
    c367::drive();
    c368::drive();
    c369::drive();
    c370::drive();
    c371::drive();
    c372::drive();
    c373::drive();
    c374::drive();
    c375::drive();
    c376::drive();
    c377::drive();
    c378::drive();
    c379::drive();
    c380::drive();
    c381::drive();
    c382::drive();
    c383::drive();
    c384::drive();
    c385::drive();
    c386::drive();
    c387::drive();
    c388::drive();
    c389::drive();
    c390::drive();
    c391::drive();
    c392::drive();
    c393::drive();
    c394::drive();
    c395::drive();
    c396::drive();
    c397::drive();
    c398::drive();
    c399::drive();
    c400::drive();
    c401::drive();
    c402::drive();
    c403::drive();
    c404::drive();
    c405::drive();
    c406::drive();
    c407::drive();
    c408::drive();
    c409::drive();
    c410::drive();
    c411::drive();
    c412::drive();
    c413::drive();
    c414::drive();
    c415::drive();
    c416::drive();
    c417::drive();
    c418::drive();
    c419::drive();
    c420::drive();
    c421::drive();
    c422::drive();
    c423::drive();
    c424::drive();
    c425::drive();
    c426::drive();
    c427::drive();
    c428::drive();
    c429::drive();
    c430::drive();
    c431::drive();
    c432::drive();
    c433::drive();
    c434::drive();
    c435::drive();
    c436::drive();
    c437::drive();
    c438::drive();
    c439::drive();
    c440::drive();
    c441::drive();
    c442::drive();
    c443::drive();
    c444::drive();
    c445::drive();
    c446::drive();
    c447::drive();
    c448::drive();
    c449::drive();
    c450::drive();
    c451::drive();
    c452::drive();
    c453::drive();
    c454::drive();
    c455::drive();
    c456::drive();
    c457::drive();
    c458::drive();
    c459::drive();
    c460::drive();
    c461::drive();
    c462::drive();
    c463::drive();
    c464::drive();
    c465::drive();
    c466::drive();
    c467::drive();
    c468::drive();
    c469::drive();
    c470::drive();
    c471::drive();
    c472::drive();
    c473::drive();
    c474::drive();
    c475::drive();
    c476::drive();
    c477::drive();
    c478::drive();
    c479::drive();
    c480::drive();
    c481::drive();
    c482::drive();
    c483::drive();
    c484::drive();
    c485::drive();
    c486::drive();
    c487::drive();
    c488::drive();
    c489::drive();
    c490::drive();
    c491::drive();
    c492::drive();
    c493::drive();
    c494::drive();
    c495::drive();
    c496::drive();
    c497::drive();
    c498::drive();
    c499::drive();
    c500::drive();
    c501::drive();
    c502::drive();
    c503::drive();
    c504::drive();
    c505::drive();
    c506::drive();
    c507::drive();
    c508::drive();
    c509::drive();
    c510::drive();
    c511::drive();
    c512::drive();
    c513::drive();
    c514::drive();
    c515::drive();
    c516::drive();
    c517::drive();
    c518::drive();
    c519::drive();
    c520::drive();
    c521::drive();
    c522::drive();
    c523::drive();
    c524::drive();
    c525::drive();
    c526::drive();
    c527::drive();
    c528::drive();
    c529::drive();
    c530::drive();
    c531::drive();
    c532::drive();
    c533::drive();
    c534::drive();
    c535::drive();
    c536::drive();
    c537::drive();
    c538::drive();
    c539::drive();
    c540::drive();
    c541::drive();
    c542::drive();
    c543::drive();
    c544::drive();
    c545::drive();
    c546::drive();
    c547::drive();
    c548::drive();
    c549::drive();
    c550::drive();
    c551::drive();
    c552::drive();
    c553::drive();
    c554::drive();
    c555::drive();
    c556::drive();
    c557::drive();
    c558::drive();
    c559::drive();
    c560::drive();
    c561::drive();
    c562::drive();
    c563::drive();
    c564::drive();
    c565::drive();
    c566::drive();
    c567::drive();
    c568::drive();
    c569::drive();
    c570::drive();
    c571::drive();
    c572::drive();
    c573::drive();
    c574::drive();
    c575::drive();
    c576::drive();
    c577::drive();
    c578::drive();
    c579::drive();
    c580::drive();
    c581::drive();
    c582::drive();
    c583::drive();
    c584::drive();
    c585::drive();
    c586::drive();
    c587::drive();
    c588::drive();
    c589::drive();
    c590::drive();
    c591::drive();
    c592::drive();
    c593::drive();
    c594::drive();
    c595::drive();
    c596::drive();
    c597::drive();
    c598::drive();
    c599::drive();
    c600::drive();
    c601::drive();
    c602::drive();
    c603::drive();
    c604::drive();
    c605::drive();
    c606::drive();
    c607::drive();
    c608::drive();
    c609::drive();
    c610::drive();
    c611::drive();
    c612::drive();
    c613::drive();
    c614::drive();
    c615::drive();
    c616::drive();
    c617::drive();
    c618::drive();
    c619::drive();
    c620::drive();
    c621::drive();
    c622::drive();
    c623::drive();
    c624::drive();
    c625::drive();
    c626::drive();
}
