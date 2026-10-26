mod collections {
    use std::marker::PhantomData;

    pub struct HashMap<K, V>(pub Vec<(K, V)>, PhantomData<K>);

    impl<K, V> Default for HashMap<K, V> {
        fn default() -> Self {
            println!("collections::HashMap::default\n{}", std::backtrace::Backtrace::force_capture());
            HashMap(Vec::new(), PhantomData)
        }
    }

    impl<K, V> HashMap<K, V> {
        pub fn with_capacity(n: usize) -> Self {
            println!("collections::HashMap::with_capacity\n{}", std::backtrace::Backtrace::force_capture());
            HashMap(Vec::with_capacity(n), PhantomData)
        }
    }
}

mod legacy {
    pub struct HashMap(pub u8);

    impl Default for HashMap {
        fn default() -> Self {
            HashMap(0)
        }
    }

    impl HashMap {
        pub fn with_capacity(n: usize) -> Self {
            HashMap(n as u8)
        }
    }
}

use collections::HashMap;

fn defaults() -> usize {
    let mut map = <HashMap<_, _>>::default();
    map.0.push((1u8, 2u8));
    map.0.len()
}

fn sized() -> usize {
    let map = <HashMap<u8, u8>>::with_capacity(4);
    map.0.capacity() + legacy::HashMap(1).0 as usize
}

fn main() {
    defaults();
    sized();
    println!("end\n{}", std::backtrace::Backtrace::force_capture());
}
