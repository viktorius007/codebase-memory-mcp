mod system {
    pub struct World {
        pub runs: u32,
    }

    pub trait RunSystemOnce: Sized {
        fn run_system_once(self, n: u32) -> u32 {
            println!("RunSystemOnce::run_system_once\n{}", std::backtrace::Backtrace::force_capture());
            self.run_system_once_with(n)
        }
        fn run_system_once_with(self, n: u32) -> u32;
    }

    impl RunSystemOnce for &mut World {
        fn run_system_once_with(self, n: u32) -> u32 {
            self.runs += n;
            self.runs
        }
    }

    pub struct Both;

    pub trait Describe: Sized {
        fn describe(self) -> u32 {
            println!("Describe::describe\n{}", std::backtrace::Backtrace::force_capture());
            self.size()
        }
        fn size(self) -> u32;
    }

    impl Describe for &Both {
        fn size(self) -> u32 {
            1
        }
    }

    impl Describe for &mut Both {
        fn size(self) -> u32 {
            2
        }
    }
}

mod app {
    use crate::system::{Both, Describe, RunSystemOnce as _, World};

    pub fn run() -> u32 {
        let mut world = World { runs: 0 };
        world.run_system_once(3)
    }

    pub fn both() -> u32 {
        let both = Both;
        both.describe()
    }
}

fn main() {
    println!("{} {}", app::run(), app::both());
}
