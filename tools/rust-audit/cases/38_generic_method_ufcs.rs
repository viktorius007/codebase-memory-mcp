pub fn f() -> usize { <Vec<u8>>::new().len() + <Vec<u8> as IntoIterator>::into_iter(Vec::new()).count() + Vec::<u8>::with_capacity(1).len() }
