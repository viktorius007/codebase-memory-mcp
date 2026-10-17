pub trait Greeter {
    fn greet(&self) -> String;
}

pub struct Loud;

impl Greeter for Loud {
    fn greet(&self) -> String {
        shout("hi")
    }
}

pub fn shout(word: &str) -> String {
    word.to_uppercase()
}

pub fn add(a: i32, b: i32) -> i32 {
    inner_add(a, b)
}

fn inner_add(a: i32, b: i32) -> i32 {
    a + b
}
