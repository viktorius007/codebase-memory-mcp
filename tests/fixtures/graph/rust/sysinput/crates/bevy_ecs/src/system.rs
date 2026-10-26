use crate::observer::On;

/// What a function becomes when it runs as a system; `In` is its input.
pub trait IntoSystem<In, Marker> {}

impl<F: Fn()> IntoSystem<(), ()> for F {}

impl<P, F: Fn(P)> IntoSystem<(), (P,)> for F where P: Default {}

impl<E, F: Fn(On<E>)> IntoSystem<On<E>, (E, bool)> for F {}
