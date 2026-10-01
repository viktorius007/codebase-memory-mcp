pub fn g() -> u8 { 1 }
pub fn f() -> (u8, u8) { loop { break (g(), 2); } }
