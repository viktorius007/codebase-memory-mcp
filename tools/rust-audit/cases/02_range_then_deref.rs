pub fn f(v: &[u8], n: &usize) -> usize { let mut c = 0; for _ in 0..*n { c += 1; } c + v[..*n].len() }
