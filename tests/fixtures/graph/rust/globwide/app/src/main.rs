use facade::prelude::*;

fn build() -> u8 {
    Cuboid::new(1).0
}

fn fallback() -> u8 {
    Cuboid::default().0
}

mod wide_user {
    use facade::wide::prelude::*;

    pub fn shape() -> u8 {
        Mesh::new(3).0
    }
}

fn main() {
    wide_user::shape();
    build();
    fallback();
    println!("end\n{}", std::backtrace::Backtrace::force_capture());
}
