//! A local module named like the dependency shadows it in a written path.
mod chk {
    macro_rules! check_eq {
        ($l:expr, $r:expr, $m:literal) => {
            stringify!($l)
        };
    }
    pub(crate) use check_eq;
}

fn nine() -> u32 { println!("nine\n{}", std::backtrace::Backtrace::force_capture()); 9 }

pub fn run() {
    println!("{}", chk::check_eq!(nine(), 9, "the local module wins"));
}
