pub static C: u8 = 1;
pub fn f() -> &'static u8 {
    &C
}
