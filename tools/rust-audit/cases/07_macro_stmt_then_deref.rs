macro_rules! m { () => {} }
pub fn f(p: &mut u8) { m! {} *p = 1; }
pub fn g(p: &mut u8, c: bool) { if c {} *p = 2; }
