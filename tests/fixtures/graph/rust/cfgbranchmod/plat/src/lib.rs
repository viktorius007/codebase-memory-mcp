/// Expands the first branch: the build this fixture models enables the feature.
#[macro_export]
macro_rules! multi_threaded {
    (if { $($a:item)* } else { $($b:item)* }) => { $($a)* };
}
