pub struct Right;
pub struct Wrong;
#[allow(non_camel_case_types)]
pub struct T;

impl Right {
    pub fn hit(&self) -> u32 {
        println!("Right::hit\n{}", std::backtrace::Backtrace::force_capture());
        1
    }
}
impl Wrong {
    pub fn hit(&self) -> u32 {
        println!("Wrong::hit\n{}", std::backtrace::Backtrace::force_capture());
        2
    }
}
impl T {
    pub fn hit(&self) -> u32 {
        println!("T::hit\n{}", std::backtrace::Backtrace::force_capture());
        3
    }
}

/* 1. The bounding trait's impl is generic; another trait names the same item. */
pub struct S;
pub trait Wanted<X> {
    type Item;
    fn make(self) -> Self::Item;
}
pub trait Other {
    type Item;
    fn other(self) -> Self::Item;
}
impl<X> Wanted<X> for S {
    type Item = Right;
    fn make(self) -> Right {
        Right
    }
}
impl Other for S {
    type Item = Wrong;
    fn other(self) -> Wrong {
        Wrong
    }
}
pub fn want<F: Wanted<u8>>(f: F) -> F::Item {
    f.make()
}

/* 2. Impls for different arguments of one self type. */
pub struct Wrap<A>(pub A);
pub trait Out {
    type Out;
    fn out(self) -> Self::Out;
}
impl Out for Wrap<u8> {
    type Out = Wrong;
    fn out(self) -> Wrong {
        Wrong
    }
}
impl Out for Wrap<Vec<u32>> {
    type Out = Right;
    fn out(self) -> Right {
        Right
    }
}
pub fn outer<F: Out>(f: F) -> F::Out {
    f.out()
}

/* 3. `Self` in the associated type is the impl's self type. */
pub trait Same {
    type Me;
    fn me(self) -> Self::Me;
}
impl Same for Right {
    type Me = Self;
    fn me(self) -> Self {
        self
    }
}
pub struct Holder;
impl Holder {
    pub fn hit(&self) -> u32 {
        println!("Holder::hit\n{}", std::backtrace::Backtrace::force_capture());
        4
    }
    pub fn conv<F: Same>(&self, f: F) -> F::Me {
        f.me()
    }
}

/* 4. A generic associated type instantiated by the projection. */
pub struct G;
pub trait Gat {
    type Item<U>;
    fn wrap<U>(self, u: U) -> Self::Item<U>;
}
impl Gat for G {
    #[allow(non_camel_case_types)]
    type Item<T> = T;
    fn wrap<U>(self, u: U) -> U {
        u
    }
}
pub fn gat<F: Gat>(f: F) -> F::Item<Right> {
    f.wrap(Right)
}

fn shape_generic_impl(s: S) -> u32 {
    want(s).hit()
}

fn shape_self_args(w: Wrap<Vec<u32>>) -> u32 {
    outer(w).hit()
}

fn shape_self_in_value(h: &Holder, r: Right) -> u32 {
    h.conv(r).hit()
}

fn shape_gat(g: G) -> u32 {
    gat(g).hit()
}

fn main() {
    let _ = Other::other(S);
    let _ = outer(Wrap(0u8));
    let total = shape_generic_impl(S) + shape_self_args(Wrap(vec![1])) + shape_self_in_value(&Holder, Right)
        + shape_gat(G) + T.hit() + Wrong.hit() + Holder.hit();
    println!("{}\n{}", total, std::backtrace::Backtrace::force_capture());
}
