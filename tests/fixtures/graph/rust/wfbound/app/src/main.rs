use bevy_asset::{Asset, AssetServer, Handle, Pinned, Slot};

pub struct Image;
impl Asset for Image {}

pub struct Pipeline {
    shader: Handle<Image>,
}

impl Pipeline {
    fn shader(&self) -> Handle<Image> {
        self.shader.clone()
    }
}

fn setup(server: &AssetServer, given: Handle<Image>) {
    let loaded: Handle<Image> = server.load("a.png");
    let copy = loaded.clone();
    let inferred = server.load::<Image>("b.png");
    let again = inferred.clone();
    let param = given.clone();
    println!("{} {} {}", copy.id(), again.id(), param.id());
}

#[derive(Default)]
pub struct Sound;
impl Asset for Sound {}

fn slots(sound: &Slot<Sound>, image: &Slot<Image>) {
    let owned = sound.clone();
    #[allow(noop_method_call, suspicious_double_ref_op)]
    let borrowed = image.clone();
    println!("{} {}", owned.value.is_some(), borrowed.value.is_some());
}

static SOUND: Sound = Sound;

fn pinned(pin: Pinned<'static, Sound>) {
    let copy = pin.clone();
    println!("{}", core::ptr::eq(copy.item, pin.item));
}

fn main() {
    let server = AssetServer;
    setup(&server, server.load("c.png"));
    let pipeline = Pipeline { shader: server.load("d.png") };
    pipeline.shader();
    slots(&Slot { value: None }, &Slot { value: None });
    pinned(Pinned { item: &SOUND });
}
