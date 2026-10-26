use core::ptr::NonNull;

pub struct Table {
    rows: u32,
}
impl Table {
    pub fn allocate(&mut self) -> u32 { println!("Table::allocate\n{}", std::backtrace::Backtrace::force_capture()); self.rows += 1; self.rows }
    pub fn len(&self) -> u32 { println!("Table::len\n{}", std::backtrace::Backtrace::force_capture()); self.rows }
}

pub struct Spawner {
    table: NonNull<Table>,
}
impl Spawner {
    pub fn spawn(&mut self) -> u32 {
        // SAFETY: the table outlives the spawner in this fixture.
        let table = unsafe { self.table.as_mut() };
        table.allocate();
        // SAFETY: as above.
        let shared = unsafe { self.table.as_ref() };
        shared.len()
    }
}

fn main() {
    let mut table = Table { rows: 0 };
    let mut spawner = Spawner { table: NonNull::from(&mut table) };
    let _ = spawner.spawn();
}
