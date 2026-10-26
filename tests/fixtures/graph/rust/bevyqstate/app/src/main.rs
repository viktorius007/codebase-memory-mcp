use core::any::{Any, TypeId};

use bevy_ecs::entity::Entity;
use bevy_ecs::query::{QueryState, With};
use bevy_ecs::system::Query;
use bevy_ecs::world::entity_access::entity_mut::EntityMut;
use bevy_ecs::world::entity_access::entity_ref::EntityRef;
use bevy_ecs::world::{Component, World};

pub struct Transform {
    x: f32,
}

impl Component for Transform {}

impl Transform {
    pub fn x(&self) -> f32 {
        println!("Transform::x\n{}", std::backtrace::Backtrace::force_capture());
        self.x
    }

    pub fn translate(&mut self, by: f32) {
        println!("Transform::translate\n{}", std::backtrace::Backtrace::force_capture());
        self.x += by;
    }
}

pub struct Velocity {
    speed: f32,
}

impl Component for Velocity {}

impl Velocity {
    pub fn speed(&self) -> f32 {
        println!("Velocity::speed\n{}", std::backtrace::Backtrace::force_capture());
        self.speed
    }
}

pub struct Player;

impl Component for Player {}

fn part<T: Component>(value: T) -> (TypeId, Box<dyn Any>) {
    (TypeId::of::<T>(), Box::new(value))
}

fn single(query: Query<&Transform, With<Player>>) {
    if let Ok(transform) = query.single() {
        transform.x();
    }
    let Ok(transform) = query.single() else {
        return;
    };
    transform.x();
}

fn get(mut query: Query<(&mut Transform, Option<&Velocity>)>, entity: Entity) {
    if let Ok((mut transform, velocity)) = query.get_mut(entity) {
        transform.translate(1.0);
        let Some(velocity) = velocity else {
            return;
        };
        velocity.speed();
    }
    if let Ok((transform, _)) = query.get(entity) {
        transform.x();
    }
    if let Ok(mut transform) = query.single_mut() {
        transform.0.translate(2.0);
    }
}

fn state(world: &mut World, entity: Entity) {
    let mut by_ref: QueryState<(Entity, &Transform)> = QueryState::new(world);
    if let Ok((_, transform)) = by_ref.get(world, entity) {
        transform.x();
    }
    let mut entities: QueryState<EntityMut> = QueryState::new(world);
    if let Ok(entity_ref) = entities.get(world, entity) {
        entity_ref.id();
    }
    if let Ok(mut entity_mut) = entities.single_mut(world) {
        entity_mut.despawn_marker();
    }
}

fn entity_refs(query: Query<EntityRef>, entity: Entity) {
    if let Ok(entity_ref) = query.get(entity) {
        entity_ref.id();
    }
}

fn main() {
    let mut world = World::default();
    let player = world.spawn(vec![
        part(Transform { x: 1.0 }),
        part(Velocity { speed: 2.0 }),
        part(Player),
    ]);
    world.spawn(vec![part(Transform { x: 3.0 })]);
    single(Query::new(&world));
    get(Query::new(&world), player);
    entity_refs(Query::new(&world), player);
    state(&mut world, player);
}
