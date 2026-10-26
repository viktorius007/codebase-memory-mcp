mod traits;
mod params;
pub use params::*;
pub trait Resource { type Mutability; }
