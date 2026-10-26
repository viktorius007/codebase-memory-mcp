pub struct Table { rows: Vec<u32> }
impl Table {
    pub fn entity_count(&self) -> usize { println!("Table::entity_count\n{}", std::backtrace::Backtrace::force_capture()); self.rows.len() }
}
pub struct Tables { tables: Vec<Table> }
impl Tables {
    pub unsafe fn get_unchecked(&self, i: usize) -> &Table { println!("Tables::get_unchecked\n{}", std::backtrace::Backtrace::force_capture()); &self.tables[i] }
}
pub struct MovingPtr(u32);
impl MovingPtr {
    pub unsafe fn from_value(v: &mut u32) -> MovingPtr { println!("MovingPtr::from_value\n{}", std::backtrace::Backtrace::force_capture()); MovingPtr(*v) }
    pub fn assign_to(&self, out: &mut u32) { println!("MovingPtr::assign_to\n{}", std::backtrace::Backtrace::force_capture()); *out = self.0; }
}
pub fn count(tables: &Tables) -> usize {
    let table = unsafe { tables.get_unchecked(0) };
    table.entity_count()
}
pub fn moved() -> u32 {
    let mut a = 7;
    let mut b = 0;
    let ptr = unsafe {
        let source = &mut a;
        MovingPtr::from_value(source)
    };
    ptr.assign_to(&mut b);
    b
}
fn main() {
    let tables = Tables { tables: vec![Table { rows: vec![1, 2] }] };
    let _ = (count(&tables), moved());
}
