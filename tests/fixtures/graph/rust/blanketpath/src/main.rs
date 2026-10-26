pub trait RelationshipTarget {
    fn len(&self) -> usize;
}

pub trait SpawnableList {
    fn spawn(self) -> usize;
}

impl SpawnableList for Vec<u8> {
    fn spawn(self) -> usize {
        self.len()
    }
}

pub trait SpawnRelated: RelationshipTarget {
    fn spawn(count: usize) -> usize;
}

impl<T: RelationshipTarget> SpawnRelated for T {
    fn spawn(count: usize) -> usize {
        println!("T::spawn\n{}", std::backtrace::Backtrace::force_capture());
        count
    }
}

pub trait Attach {
    fn attach() -> usize;
}

impl<T: RelationshipTarget> Attach for T {
    fn attach() -> usize {
        println!("T::attach\n{}", std::backtrace::Backtrace::force_capture());
        1
    }
}

pub trait Marker {}

/* A second in-scope trait whose `attach` has a default body, reaching its
 * types through an impl that defines nothing: the rule withholds. */
pub trait DefaultAttach {
    fn attach() -> usize {
        0
    }
}

impl<T: Marker> DefaultAttach for T {}

pub struct Children(Vec<u8>);

impl RelationshipTarget for Children {
    fn len(&self) -> usize {
        self.0.len()
    }
}

fn build() -> usize {
    Children::spawn(2)
}

fn attach() -> usize {
    Children::attach()
}

fn main() {
    build();
    attach();
    println!("{}", vec![1u8].spawn() + Children(Vec::new()).len());
    println!("end\n{}", std::backtrace::Backtrace::force_capture());
}
