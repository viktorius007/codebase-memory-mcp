trait Trait<'b> {} struct S<'a> { pub baz: &'a (dyn Unpin + for<'b> Trait<'b>), }
