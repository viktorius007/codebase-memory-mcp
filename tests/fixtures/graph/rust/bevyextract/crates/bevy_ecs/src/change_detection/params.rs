use core::ops::{Deref, DerefMut};

pub struct Mut<'w, T: ?Sized> {
    pub(crate) value: &'w mut T,
    pub(crate) changed: bool,
}

impl<'w, T: ?Sized> Mut<'w, T> {
    pub fn set_changed(&mut self) {
        println!("Mut::set_changed\n{}", std::backtrace::Backtrace::force_capture());
        self.changed = true;
    }
}

impl<'w, T: ?Sized> Deref for Mut<'w, T> {
    type Target = T;
    fn deref(&self) -> &T {
        self.value
    }
}

impl<'w, T: ?Sized> DerefMut for Mut<'w, T> {
    fn deref_mut(&mut self) -> &mut T {
        self.changed = true;
        self.value
    }
}

pub struct Ref<'w, T: ?Sized> {
    pub(crate) value: &'w T,
    pub(crate) added: bool,
}

impl<'w, T: ?Sized> Ref<'w, T> {
    pub fn is_added(&self) -> bool {
        println!("Ref::is_added\n{}", std::backtrace::Backtrace::force_capture());
        self.added
    }
}

impl<'w, T: ?Sized> Deref for Ref<'w, T> {
    type Target = T;
    fn deref(&self) -> &T {
        self.value
    }
}
