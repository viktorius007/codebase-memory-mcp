pub struct Thing;
impl Thing {
    pub fn ping(&self) { println!("dep::Thing::ping\n{}", std::backtrace::Backtrace::force_capture()); }
}
