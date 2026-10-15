#[derive(Clone, Copy)] pub struct Store;
impl Store { pub fn act(self) { println!("Store::act inherent (self)\n{}", std::backtrace::Backtrace::force_capture()); } }
pub trait StoreTrait { fn act(&self); }
impl StoreTrait for Store { fn act(&self) { println!("<Store as StoreTrait>::act (&self)\n{}", std::backtrace::Backtrace::force_capture()); } }
#[derive(Clone, Copy)] pub struct Own;
impl Own { pub fn go(&self) { println!("Own::go inherent (&self)\n{}", std::backtrace::Backtrace::force_capture()); } }
pub trait OwnTrait { fn go(self); }
impl OwnTrait for Own { fn go(self) { println!("<Own as OwnTrait>::go (self)\n{}", std::backtrace::Backtrace::force_capture()); } }
