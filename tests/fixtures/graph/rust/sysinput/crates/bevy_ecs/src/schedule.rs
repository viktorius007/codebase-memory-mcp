use crate::system::IntoSystem;

pub trait IntoScheduleConfigs<Marker>: Sized {
    fn run_if(self, condition: bool) -> Self {
        println!("IntoScheduleConfigs::run_if\n{}", std::backtrace::Backtrace::force_capture());
        let _ = condition;
        self
    }
}

impl<Marker, F: IntoSystem<(), Marker>> IntoScheduleConfigs<Marker> for F {}
