mod fetch;
use fetch::{Entity, World};

fn by_argument(world: &mut World, first: Entity, second: Entity) -> u32 {
    let a = world.entity(first).id();
    let b = world.entity_mut(second).insert(3);
    a + b
}

fn by_binding(world: &mut World, entity: &Entity) -> u32 {
    let e = *entity;
    let mut target = world.entity_mut(e);
    target.insert(5)
}

fn through_result(world: &mut World, entity: Entity) -> u32 {
    if let Ok(mut target) = world.get_entity_mut(entity) {
        return target.insert(7);
    }
    0
}

fn by_turbofish(world: &mut World) -> u32 {
    world.spawn_one::<Entity>().insert(8)
}

fn main() {
    let mut world = World { count: 0 };
    let total = by_argument(&mut world, Entity(1), Entity(2))
        + by_binding(&mut world, &Entity(4))
        + through_result(&mut world, Entity(6))
        + by_turbofish(&mut world);
    println!("{}\n{}", total, std::backtrace::Backtrace::force_capture());
}
