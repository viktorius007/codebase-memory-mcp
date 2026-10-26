pub trait Curve {
    fn domain(&self) -> u32;
}

pub trait Sampler {
    fn sample(&self) -> u32;
}

pub struct Line;

impl Curve for Line {
    fn domain(&self) -> u32 {
        println!("Line::domain\n{}", std::backtrace::Backtrace::force_capture());
        1
    }
}

impl Sampler for Line {
    fn sample(&self) -> u32 {
        println!("Line::sample\n{}", std::backtrace::Backtrace::force_capture());
        2
    }
}

pub struct MapCurve<C> {
    preimage: C,
}

impl<C> Curve for MapCurve<C>
where
    C: Curve,
{
    fn domain(&self) -> u32 {
        self.preimage.domain()
    }
}

pub struct Pair<'a, C, D> {
    first: &'a C,
    second: D,
}

impl<'a, X: Sampler, C: Curve> Pair<'a, C, X> {
    pub fn total(&self) -> u32 {
        self.first.domain() + self.second.sample()
    }
}

fn main() {
    let map = MapCurve { preimage: Line };
    let line = Line;
    let pair = Pair { first: &line, second: Line };
    println!("{}\n{}", map.domain() + pair.total(), std::backtrace::Backtrace::force_capture());
}
