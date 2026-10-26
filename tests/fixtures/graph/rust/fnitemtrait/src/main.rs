pub struct Built;
impl Built {
    pub fn finish(self) -> u32 {
        println!("Built::finish\n{}", std::backtrace::Backtrace::force_capture());
        4
    }
}

pub trait Configs<M> {
    fn into_c(self) -> u32;
    fn configure(self) -> Built
    where
        Self: Sized,
    {
        println!("Configs::configure default\n{}", std::backtrace::Backtrace::force_capture());
        Built
    }
    fn after(self) -> u32
    where
        Self: Sized,
    {
        println!("Configs::after default\n{}", std::backtrace::Backtrace::force_capture());
        1
    }
}
impl<F: Fn() -> u8> Configs<u8> for F {
    fn into_c(self) -> u32 {
        0
    }
}
impl<A: Fn() -> u8, B: Fn() -> u8> Configs<(u8, u8)> for (A, B) {
    fn into_c(self) -> u32 {
        0
    }
}

pub trait Required {
    fn required(self);
}
impl<F: Fn() -> u8> Required for F {
    fn required(self) {
        println!("Required::required blanket\n{}", std::backtrace::Backtrace::force_capture());
    }
}

pub trait Deco {
    fn deco(self)
    where
        Self: Sized,
    {
        println!("Deco::deco default\n{}", std::backtrace::Backtrace::force_capture());
    }
}
impl<F: Fn() -> u8> Deco for F {
    fn deco(self) {
        println!("Deco::deco blanket\n{}", std::backtrace::Backtrace::force_capture());
    }
}

pub trait Twin {
    fn twin(self)
    where
        Self: Sized,
    {
        println!("Twin::twin default\n{}", std::backtrace::Backtrace::force_capture());
    }
}
impl<F: Fn() -> u8> Twin for F {}
pub trait TwinOther {
    fn twin(self)
    where
        Self: Sized,
    {
        println!("TwinOther::twin default\n{}", std::backtrace::Backtrace::force_capture());
    }
}
impl TwinOther for u32 {}

pub trait Sched {}
pub struct SysKind;
impl Sched for SysKind {}
pub struct Steps<T>(Option<T>);
pub trait IntoSteps<T: Sched> {
    fn step(self) -> Steps<T>
    where
        Self: Sized,
    {
        println!("IntoSteps::step default\n{}", std::backtrace::Backtrace::force_capture());
        Steps(None)
    }
}
impl<F: Fn() -> u8> IntoSteps<SysKind> for F {}
impl<T: Sched> IntoSteps<T> for Steps<T> {
    fn step(self) -> Steps<T> {
        println!("Steps::step\n{}", std::backtrace::Backtrace::force_capture());
        self
    }
}

fn sys() -> u8 {
    1
}
fn other() -> u8 {
    2
}

fn single() -> u32 {
    sys.after()
}

fn tupled() -> u32 {
    (sys, other).after()
}

fn stepped() -> u32 {
    let steps = sys.step().step();
    steps.0.map_or(0, |_| 1)
}

fn tupled_chain() -> u32 {
    (sys, other).configure().finish()
}

fn withheld() {
    sys.required();
    sys.deco();
    other.twin();
}

fn main() {
    let configured = single() + stepped() + tupled() + tupled_chain() + sys.into_c() + sys() as u32 + other() as u32;
    withheld();
    println!("{configured}");
}
