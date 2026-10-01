pub fn f(s: usize, n: usize) -> core::ops::Range<usize> { let r; r = s..(s + n); r }
pub fn h(v: Vec<usize>) -> Vec<core::ops::Range<usize>> { v.into_iter().map(|i| (i + 1)..(i + 2)).collect() }
