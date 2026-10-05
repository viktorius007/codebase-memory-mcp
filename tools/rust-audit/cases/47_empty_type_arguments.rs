struct Vehicle; type Bike = Vehicle<>; mod epsilon { pub struct Foo; } pub struct H(epsilon::Foo::<>); type K = u8<>;
