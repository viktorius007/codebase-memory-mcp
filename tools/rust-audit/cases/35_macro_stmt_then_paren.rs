macro_rules! m { () => {} }
pub fn f(a: u8) -> u8 { m! {} (a + 1) }
pub fn g(a: &u8) -> &u8 { m! {} &a }
