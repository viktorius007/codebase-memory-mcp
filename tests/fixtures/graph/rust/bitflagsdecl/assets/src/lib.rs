mod render_asset;

pub use render_asset::*;

pub mod other {
    pub struct RenderAssetUsages(pub u8);

    impl Default for RenderAssetUsages {
        fn default() -> Self {
            RenderAssetUsages(0)
        }
    }
}
