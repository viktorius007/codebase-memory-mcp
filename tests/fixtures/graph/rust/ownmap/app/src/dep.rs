pub struct Thing;
impl Thing {
    pub fn ping(&self) { println!("app::dep::Thing::ping\n{}", std::backtrace::Backtrace::force_capture()); }
}
