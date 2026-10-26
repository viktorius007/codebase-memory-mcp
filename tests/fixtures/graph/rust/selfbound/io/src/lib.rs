pub trait AssetReader {
    fn read(&self, path: u8) -> u8;
    fn is_directory(&self, path: u8) -> bool;
}

pub trait ErasedAssetReader {
    fn read(&self, path: u8) -> u8;
    fn is_directory(&self, path: u8) -> bool;
    fn label(&self) -> u8;
}

impl<T: AssetReader> ErasedAssetReader for T {
    fn read(&self, path: u8) -> u8 {
        Self::read(self, path)
    }
    fn is_directory(&self, path: u8) -> bool {
        let found = Self::is_directory(self, path);
        found
    }
    fn label(&self) -> u8 {
        Self::read(self, 0)
    }
}

pub struct FileReader;

impl AssetReader for FileReader {
    fn read(&self, path: u8) -> u8 {
        println!("FileReader::read\n{}", std::backtrace::Backtrace::force_capture());
        path
    }
    fn is_directory(&self, path: u8) -> bool {
        println!("FileReader::is_directory\n{}", std::backtrace::Backtrace::force_capture());
        path == 0
    }
}
