use core::any::{Any, TypeId};

use bevy_ecs::change_detection::Ref;
use bevy_ecs::entity::Entity;
use bevy_ecs::query::Has;
use bevy_ecs::system::Query;
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

fn for_iter(query: Query<(Entity, &Transform, Option<&Velocity>)>) {
    for (entity, transform, velocity) in query.iter() {
        entity.index();
        transform.x();
        if let Some(velocity) = velocity {
            velocity.speed();
        }
    }
}

fn for_ref(query: Query<&Transform>) {
    for transform in &query {
        transform.x();
    }
}

fn for_mut(mut query: Query<(&mut Transform, &Velocity)>) {
    for (mut transform, velocity) in &mut query {
        transform.translate(velocity.speed());
        transform.set_changed();
    }
    for (mut transform, velocity) in query.iter_mut() {
        transform.translate(velocity.speed());
    }
    for (transform, velocity) in query.iter() {
        transform.x();
        velocity.speed();
    }
}

fn singleton(query: Query<(&Transform,)>) {
    for (transform,) in &query {
        transform.x();
    }
}

fn closures(query: Query<(&Transform, &Velocity)>) {
    query.iter().for_each(|(transform, velocity)| {
        transform.x();
        velocity.speed();
    });
    let total: f32 = query.iter().map(|(transform, _)| transform.x()).sum();
    println!("{total}");
    if let Some((transform, _)) = query.iter().next() {
        transform.x();
    }
}

fn wrappers(query: Query<(Ref<Transform>, Has<Player>)>) {
    for (transform, is_player) in &query {
        if is_player && transform.is_added() {
            transform.x();
        }
    }
}

fn main() {
    let mut world = World::default();
    world.spawn(vec![
        part(Transform { x: 1.0 }),
        part(Velocity { speed: 2.0 }),
        part(Player),
    ]);
    world.spawn(vec![part(Transform { x: 3.0 })]);
    for_iter(Query::new(&world));
    for_ref(Query::new(&world));
    for_mut(Query::new(&world));
    singleton(Query::new(&world));
    closures(Query::new(&world));
    wrappers(Query::new(&world));
}
