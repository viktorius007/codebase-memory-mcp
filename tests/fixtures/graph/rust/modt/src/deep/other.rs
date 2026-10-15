pub fn helper() { println!("other::helper\n{}", std::backtrace::Backtrace::force_capture()); }
pub fn call() { helper(); crate::b::f(); }
