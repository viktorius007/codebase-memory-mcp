use bevy_ecs::prelude::*;

pub struct Click;

fn tick() {}

fn count(steps: u32) {
    let _ = steps;
}

fn on_click(trigger: On<Click>) {
    let On { event: Click } = trigger;
}

fn main() {
    let _ = tick.run_if(true);
    let _ = count.run_if(false);
    let _ = on_click.run_if(true);
}
