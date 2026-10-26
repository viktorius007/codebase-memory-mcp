pub struct DeferredWorld<'w> {
    count: &'w mut u32,
}
impl<'w> DeferredWorld<'w> {
    pub fn bump(&mut self) -> u32 { println!("DeferredWorld::bump\n{}", std::backtrace::Backtrace::force_capture()); *self.count += 1; *self.count }
}
pub struct HookContext {
    pub entity: u32,
}
impl HookContext {
    pub fn entity(&self) -> u32 { println!("HookContext::entity\n{}", std::backtrace::Backtrace::force_capture()); self.entity }
}
pub type ComponentHook = for<'w> fn(DeferredWorld<'w>, HookContext);

#[derive(Default)]
pub struct Hooks {
    on_add: Option<ComponentHook>,
}
impl Hooks {
    pub fn on_add(&mut self, hook: ComponentHook) -> &mut Self {
        self.on_add = Some(hook);
        self
    }
    pub fn run(&self, count: &mut u32) {
        if let Some(hook) = self.on_add {
            hook(DeferredWorld { count }, HookContext { entity: 7 });
        }
    }
}

fn main() {
    let mut hooks = Hooks::default();
    hooks.on_add(|mut world, context| {
        world.bump();
        context.entity();
    });
    let mut count = 0;
    hooks.run(&mut count);
}
