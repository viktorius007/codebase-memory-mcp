use assets::RenderAssetUsages;

fn usage() -> u8 {
    RenderAssetUsages::default().bits() + assets::other::RenderAssetUsages(1).0
}

fn main() {
    usage();
    println!("end\n{}", std::backtrace::Backtrace::force_capture());
}
