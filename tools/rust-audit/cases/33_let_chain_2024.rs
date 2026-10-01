pub fn f(a: Option<u8>, b: Option<u8>) -> u8 { if let Some(x) = a && x > 1 && let Some(y) = b { x + y } else { 0 } }
