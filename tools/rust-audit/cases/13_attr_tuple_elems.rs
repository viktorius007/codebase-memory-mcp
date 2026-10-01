pub fn f() -> (u8, u8) { let _a = [#[cfg(all())] 1u8, #[cfg(all())] 2u8]; (1, #[cfg(all())] 2) }
