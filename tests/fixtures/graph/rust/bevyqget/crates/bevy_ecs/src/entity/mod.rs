#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub struct Entity(pub(crate) usize);

impl Entity {
    pub fn index(&self) -> usize {
        println!("Entity::index\n{}", std::backtrace::Backtrace::force_capture());
        self.0
    }
}
