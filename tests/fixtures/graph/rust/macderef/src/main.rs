mod cd;
use cd::{Res, ResMut, Resource};
pub struct Server;
impl Resource for Server { type Mutability = cd::Mutable; }
impl Server {
    pub fn load(&self) -> u32 {
        println!("Server::load\n{}", std::backtrace::Backtrace::force_capture());
        1
    }
}
fn sys(server: Res<Server>, other: ResMut<Server>) -> u32 {
    server.load() + other.load()
}
fn main() {
    let s = Server;
    println!("{}\n{}", sys(Res { value: &s }, ResMut { value: &s }), std::backtrace::Backtrace::force_capture());
}
