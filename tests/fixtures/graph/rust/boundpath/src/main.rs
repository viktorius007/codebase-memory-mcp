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

pub trait IntoSystem {
    fn into_system(this: Self) -> u32;
}

pub trait System {}

impl<T: System> IntoSystem for T {
    fn into_system(_this: Self) -> u32 {
        0
    }
}

fn boxed<H: IntoSystem + Send>(handler: H) -> u32 {
    IntoSystem::into_system(handler)
}

pub trait Section {
    fn name(&self) -> u32;
}

pub trait ErasedSection {
    fn snapshot(&self) -> u32;
}

impl<S: Section> ErasedSection for S {
    fn snapshot(&self) -> u32 {
        println!("S::snapshot\n{}", std::backtrace::Backtrace::force_capture());
        self.name()
    }
}

fn snapshot_value<S: Section>(section: &S) -> u32 {
    ErasedSection::snapshot(section)
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

impl IntoSystem for Window {
    fn into_system(this: Self) -> u32 {
        println!("Window::into_system\n{}", std::backtrace::Backtrace::force_capture());
        this.0
    }
}

impl Section for Window {
    fn name(&self) -> u32 {
        self.0
    }
}

fn main() {
    snapshot_value(&Window(5));
    boxed(Window(3));
    let group = <ReflectSettingsGroup as CreateTypeData<Window>>::create_type_data();
    let buffer = AtomicRawBufferVec::<Window> { values: vec![7], marker: None };
    let window = buffer.get(0);
    println!("{} {} {}", group.name, group.path, window.0);
    println!("end\n{}", std::backtrace::Backtrace::force_capture());
}
