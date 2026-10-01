macro_rules! mac { ($t:ty) => { $t } }
pub fn f() where mac!(u8): Send {}
pub fn after() {}
