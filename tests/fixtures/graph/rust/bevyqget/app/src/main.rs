use core::any::{Any, TypeId};

use bevy_ecs::entity::Entity;
use bevy_ecs::query::With;
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
}
