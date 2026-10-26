use crate::system::IntoSystem;

/// The input an observer system receives.
pub struct On<E> {
    pub event: E,
}

pub trait ObserverSystemExt<E, Marker>: Sized {
    fn run_if(self, condition: bool) -> Self {
        println!("ObserverSystemExt::run_if\n{}", std::backtrace::Backtrace::force_capture());
        let _ = condition;
        self
    }
}

impl<E, Marker, F: IntoSystem<On<E>, Marker>> ObserverSystemExt<E, Marker> for F {}
