// Collapsed so the run reports the expanded call at the invocation, as
// rust-analyzer reports the method name there.
#[collapse_debuginfo(yes)]
macro_rules! invoke {
    ($seen:expr, $name:literal, $receiver:ident.$method:ident($($arg:expr),* $(,)?)) => {{
        let value = $receiver.$method($($arg),*).unwrap();
        $seen.push(($name, stringify!($method)));
        value
    }};
}

pub(super) use invoke;
