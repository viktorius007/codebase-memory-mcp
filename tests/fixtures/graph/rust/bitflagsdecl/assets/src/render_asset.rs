flags::bitflags! {
    /// Where an asset is kept.
    pub struct RenderAssetUsages: u8 {
        const MAIN_WORLD = 1;
        const RENDER_WORLD = 2;
    }
}

impl Default for RenderAssetUsages {
    fn default() -> Self {
        println!("RenderAssetUsages::default\n{}", std::backtrace::Backtrace::force_capture());
        RenderAssetUsages::MAIN_WORLD
    }
}
