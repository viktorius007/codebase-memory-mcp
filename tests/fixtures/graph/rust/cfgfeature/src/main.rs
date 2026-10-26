#[cfg(feature = "fast")]
mod fast_ops {
    pub fn op() -> u32 {
        println!("fast_ops::op\n{}", std::backtrace::Backtrace::force_capture());
        1
    }
}

#[cfg(not(feature = "fast"))]
mod slow_ops {
    pub fn op() -> u32 {
        println!("slow_ops::op\n{}", std::backtrace::Backtrace::force_capture());
        2
    }
}

#[cfg(feature = "fast")]
pub use fast_ops::*;
#[cfg(not(feature = "fast"))]
pub use slow_ops::*;

mod lock {
    #[cfg(feature = "fast")]
    use crate::fast_ops as implementation;

    #[cfg(not(feature = "fast"))]
    mod implementation {
        pub fn acquire() -> u32 {
            println!("lock::implementation::acquire\n{}", std::backtrace::Backtrace::force_capture());
            3
        }
    }

    pub fn take() -> u32 {
        implementation::acquire()
    }
}

fn run_op() -> u32 {
    op()
}

fn main() {
    let total = run_op() + lock::take();
    println!("{total}");
}
