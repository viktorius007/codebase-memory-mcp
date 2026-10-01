macro_rules! tok { [~] => { 1 }; [+] => { 2 }; ($ name : ty) => { 3 }; }
pub fn after() -> u8 { tok![~] + tok![+] }
