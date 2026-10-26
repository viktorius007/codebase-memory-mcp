pub trait Visit {
    fn visit(&self) -> u32;
}
pub struct Dep(u32);
impl Visit for Dep {
    fn visit(&self) -> u32 { println!("Dep::visit\n{}", std::backtrace::Backtrace::force_capture()); self.0 }
}
impl Dep {
    pub fn weight(&self) -> u32 { println!("Dep::weight\n{}", std::backtrace::Backtrace::force_capture()); 2 }
}
impl Visit for Vec<Dep> {
    fn visit(&self) -> u32 {
        let mut n = 0;
        for dep in self {
            n += dep.weight();
        }
        n
    }
}
impl Visit for Option<Dep> {
    fn visit(&self) -> u32 {
        if let Some(dep) = self {
            return dep.weight();
        }
        0
    }
}
fn main() {
    let v = vec![Dep(1), Dep(2)];
    let o = Some(Dep(3));
    let _ = (v.visit(), o.visit(), Dep(4).visit());
}
