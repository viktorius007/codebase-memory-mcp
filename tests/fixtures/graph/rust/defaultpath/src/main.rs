pub trait States {}

pub trait FreelyMutableState: States {
    fn register_state(schedule: &mut Vec<u8>) {
        println!("FreelyMutableState::register_state\n{}", std::backtrace::Backtrace::force_capture());
        schedule.push(1);
    }
}

enum SimpleState {
    A,
}

/* Stands in for `#[derive(States)]`: the impls are generated, not written. */
macro_rules! derive_states {
    ($t:ident) => {
        impl States for $t {}
        impl FreelyMutableState for $t {}
    };
}

derive_states!(SimpleState);

fn setup(schedule: &mut Vec<u8>) {
    SimpleState::register_state(schedule);
}

fn main() {
    let mut schedule = Vec::new();
    setup(&mut schedule);
    let _ = SimpleState::A;
    println!("end\n{}", std::backtrace::Backtrace::force_capture());
}
