use bevy_ecs::prelude::*;

pub struct Click;

fn tick() {}

fn count(steps: u32) {
    let _ = steps;
}

fn on_click(trigger: On<Click>) {
    let On { event: Click } = trigger;
}

fn nested() {
    fn local_tick() {}
    fn local_click(trigger: On<Click>) {
        let On { event: Click } = trigger;
    }
    let _ = local_tick.run_if(true);
    let _ = local_click.run_if(false);
}

fn main() {
    let _ = tick.run_if(true);
    let _ = count.run_if(false);
    let _ = on_click.run_if(true);
    nested();
}
