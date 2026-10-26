pub trait Resource {}
pub trait TypePath {
    fn type_path() -> &'static str;
}
pub trait SettingsGroup: Resource {
    fn settings_group_name() -> &'static str;
}
pub trait AtomicPod: Default {
    fn read_from_blob(blob: &u32) -> Self;
}

pub trait CreateTypeData<T> {
    fn create_type_data() -> Self;
}

pub struct ReflectSettingsGroup {
    pub name: &'static str,
    pub path: &'static str,
}

impl<T: SettingsGroup + TypePath + std::fmt::Debug + Send> CreateTypeData<T> for ReflectSettingsGroup {
    fn create_type_data() -> Self {
        ReflectSettingsGroup { name: T::settings_group_name(), path: T::type_path() }
    }
}

pub struct AtomicRawBufferVec<T> {
    values: Vec<u32>,
    marker: Option<T>,
}

impl<T> AtomicRawBufferVec<T>
where
    T: AtomicPod + Copy + std::fmt::Debug,
{
    pub fn get(&self, index: usize) -> T {
        T::read_from_blob(&self.values[index])
    }
}

#[derive(Default, Clone, Copy, Debug)]
struct Window(u32);
impl Resource for Window {}
impl SettingsGroup for Window {
    fn settings_group_name() -> &'static str {
        println!("Window::settings_group_name\n{}", std::backtrace::Backtrace::force_capture());
        "window"
    }
}
impl TypePath for Window {
    fn type_path() -> &'static str {
        println!("Window::type_path\n{}", std::backtrace::Backtrace::force_capture());
        "Window"
    }
}
impl AtomicPod for Window {
    fn read_from_blob(blob: &u32) -> Self {
        println!("Window::read_from_blob\n{}", std::backtrace::Backtrace::force_capture());
        Window(*blob)
    }
}

fn main() {
    let group = <ReflectSettingsGroup as CreateTypeData<Window>>::create_type_data();
    let buffer = AtomicRawBufferVec::<Window> { values: vec![7], marker: None };
    let window = buffer.get(0);
    println!("{} {} {}", group.name, group.path, window.0);
    println!("end\n{}", std::backtrace::Backtrace::force_capture());
}
