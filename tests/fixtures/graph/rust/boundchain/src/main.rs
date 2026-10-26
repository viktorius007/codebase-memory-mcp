use std::marker::PhantomData;

pub struct Interval {
    start: f32,
    end: f32,
}
impl Interval {
    pub fn end(&self) -> f32 { println!("Interval::end\n{}", std::backtrace::Backtrace::force_capture()); self.end }
    pub fn start(&self) -> f32 { println!("Interval::start\n{}", std::backtrace::Backtrace::force_capture()); self.start }
}

pub trait Curve<T> {
    fn domain(&self) -> Interval;
    fn sample(&self, t: f32) -> T;
}

pub struct Line;
impl Curve<f32> for Line {
    fn domain(&self) -> Interval { Interval { start: 0.0, end: 1.0 } }
    fn sample(&self, t: f32) -> f32 { t }
}

pub struct Chain<T, C, D> {
    first: C,
    second: D,
    marker: PhantomData<T>,
}
impl<T, C, D> Curve<T> for Chain<T, C, D>
where
    C: Curve<T>,
    D: Curve<T>,
{
    fn domain(&self) -> Interval {
        Interval { start: self.first.domain().start(), end: self.first.domain().end() + self.second.domain().end() }
    }
    fn sample(&self, t: f32) -> T { self.second.sample(t) }
}

fn main() {
    let chain = Chain { first: Line, second: Line, marker: PhantomData };
    let _ = (chain.domain().end(), chain.sample(0.5));
}
