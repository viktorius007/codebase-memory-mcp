pub mod observer;
pub mod schedule;
pub mod system;

pub mod prelude {
    pub use crate::observer::{ObserverSystemExt, On};
    pub use crate::schedule::IntoScheduleConfigs;
    pub use crate::system::IntoSystem;
}
