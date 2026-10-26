//! Re-exports the dependency's prelude through a glob chain.
pub use eco as ecs;

pub mod prelude {
    pub use crate::ecs::prelude::*;
}
