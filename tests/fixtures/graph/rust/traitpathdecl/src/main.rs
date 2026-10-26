pub trait Store {
    fn get(&mut self) -> u32;
}
pub trait Exec: Store {}

pub struct Mem;
impl Store for Mem {
    fn get(&mut self) -> u32 {
        println!("Mem::get\n{}", std::backtrace::Backtrace::force_capture());
        1
    }
}
impl Exec for Mem {}

pub struct Disk;
impl Store for Disk {
    fn get(&mut self) -> u32 {
        println!("Disk::get\n{}", std::backtrace::Backtrace::force_capture());
        2
    }
}
impl Exec for Disk {}

pub trait Caps {
    fn fetch(&mut self) -> u32;
}
impl<T: Exec + ?Sized> Caps for T {
    fn fetch(&mut self) -> u32 {
        Store::get(self)
    }
}

pub trait Erased {
    fn erased(&self) -> u32;
}
impl<T: Exec + ?Sized> Erased for T {
    fn erased(&self) -> u32 {
        println!("Erased::erased blanket\n{}", std::backtrace::Backtrace::force_capture());
        5
    }
}

fn via_generic<S: Exec>(store: &S) -> u32 {
    Erased::erased(store)
}

fn via_dyn(store: &mut dyn Exec) -> u32 {
    Store::get(store)
}

fn via_concrete(mem: &mut Mem) -> u32 {
    Store::get(mem)
}

fn main() {
    let total = via_dyn(&mut Mem) + via_dyn(&mut Disk) + via_concrete(&mut Mem) + Disk.fetch() + via_generic(&Mem);
    println!("{total}");
}
