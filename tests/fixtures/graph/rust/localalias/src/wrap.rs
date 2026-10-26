use core::ops::Deref;

pub struct Local<'s, T> {
    pub value: &'s T,
}

impl<'s, T> Deref for Local<'s, T> {
    type Target = T;

    fn deref(&self) -> &T {
        self.value
    }
}
