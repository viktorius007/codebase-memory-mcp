pub enum GltfAssetLabel {
    Scene(u8),
    Primitive { mesh: u8, primitive: u8 },
}

impl GltfAssetLabel {
    pub fn from_asset(self, path: &str) -> usize {
        println!("GltfAssetLabel::from_asset\n{}", std::backtrace::Backtrace::force_capture());
        match self {
            GltfAssetLabel::Scene(n) => n as usize + path.len(),
            GltfAssetLabel::Primitive { mesh, primitive } => (mesh + primitive) as usize,
        }
    }
}

impl Default for GltfAssetLabel {
    fn default() -> Self {
        Self::Primitive { mesh: 0, primitive: 0 }
    }
}

fn load() -> usize {
    GltfAssetLabel::Primitive { mesh: 0, primitive: 1 }.from_asset("cube.gltf")
}

fn main() {
    load();
    println!("end\n{}", std::backtrace::Backtrace::force_capture());
}
