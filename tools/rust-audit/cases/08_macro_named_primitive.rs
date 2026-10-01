macro_rules! str { ($($t:tt)*) => { "" } }
pub fn f() -> &'static str { str![[r#"x"#]] }
pub fn g() -> &'static str { str!("y") }
