use io::{ErasedAssetReader, FileReader};

fn erased(reader: &dyn ErasedAssetReader) -> u8 {
    reader.read(1) + reader.label() + reader.is_directory(2) as u8
}

fn main() {
    erased(&FileReader);
    println!("end\n{}", std::backtrace::Backtrace::force_capture());
}
