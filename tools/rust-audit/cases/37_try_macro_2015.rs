pub fn f(r: Result<u8, ()>) -> Result<u8, ()> { let v = try!(r); Ok(v) }
