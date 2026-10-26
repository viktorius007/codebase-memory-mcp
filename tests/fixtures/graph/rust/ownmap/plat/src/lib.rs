macro_rules! enabled {
    ($($p:tt)*) => { $($p)* };
}
enabled! {
    pub mod collections;
}
