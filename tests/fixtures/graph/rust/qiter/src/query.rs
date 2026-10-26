pub struct Query {
    items: Vec<u8>,
}

impl Query {
    pub fn new(items: Vec<u8>) -> Query {
        println!("Query::new\n{}", std::backtrace::Backtrace::force_capture());
        Query { items }
    }

    pub fn iter(&self) -> std::slice::Iter<'_, u8> {
        println!("Query::iter\n{}", std::backtrace::Backtrace::force_capture());
        self.items.iter()
    }

    pub fn iter_mut(&mut self) -> std::slice::IterMut<'_, u8> {
        println!("Query::iter_mut\n{}", std::backtrace::Backtrace::force_capture());
        self.items.iter_mut()
    }
}
