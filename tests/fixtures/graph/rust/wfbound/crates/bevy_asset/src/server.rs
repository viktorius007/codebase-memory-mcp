use crate::handle::{Asset, Handle};

pub struct AssetServer;

impl AssetServer {
    pub fn load<A: Asset>(&self, path: &str) -> Handle<A> {
        Handle::Strong(path.len() as u32)
    }
}
