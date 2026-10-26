/* Closed table of standard-library projections used by Rust receiver typing.
 * Admitted assumption (2026-10-26 accuracy run, lane w2-std): no std source is
 * read at index time; a std type or trait member absent from this table stays
 * unknown. Each row names the std type, the member it describes and the
 * documentation it was taken from. The hashbrown rows describe the crate std's
 * maps are built on; its Entry API is the one std re-exports, member for member.
 *
 * CBM_STD_ENTRY_OF(map, method, entry): `map.method(k)` yields the map's Entry
 *   over the map's <K, V> arguments.
 * CBM_STD_ENTRY_VALUE(entry, method): `entry.method(..)` yields `&mut V`, V the
 *   entry's second type argument.
 * CBM_STD_INDEX_VALUE(map): `map[&k]` is the map's `Index::Output`, its second
 *   type argument.
 * CBM_STD_ITER_ITEM(iter, item): the `Iterator::Item` of a map or set iterator
 *   type, written over its own type arguments: T the first (K, or a set's T),
 *   E the second (V); `&` shared, `!` mutable borrow; "a,b" a pair.
 * CBM_STD_SEQ_ELEM(method, ret, source): `v.method(i)` on a Vec or slice whose
 *   index argument is not a range returns `ret` (rust_std_methods.h syntax,
 *   T the element).
 * CBM_STD_GUARD_LEAF(leaf, qn, source): a lock guard type that no corpus type
 *   claims and whose path the use graph cannot settle (a cfg-selected
 *   re-export) is read by its last segment as the std guard `qn`, which
 *   derefs to its first type argument. std, parking_lot, spin and tokio
 *   guards of these names all deref to the guarded T.
 * CBM_STD_PRODUCER(module, fn, ret, source, date): rows added to the producer
 *   list rust_std_methods.h documents (same syntax; `~A` is the referent of the
 *   first argument's type). */

#ifdef CBM_STD_ENTRY_OF
CBM_STD_ENTRY_OF("std.collections.HashMap", "entry", "std.collections.hash_map.Entry",
                 "https://doc.rust-lang.org/std/collections/struct.HashMap.html#method.entry")
CBM_STD_ENTRY_OF("std.collections.BTreeMap", "entry", "std.collections.btree_map.Entry",
                 "https://doc.rust-lang.org/std/collections/struct.BTreeMap.html#method.entry")
#endif

#ifdef CBM_STD_ENTRY_VALUE
CBM_STD_ENTRY_VALUE("std.collections.hash_map.Entry", "or_insert",
                    "https://doc.rust-lang.org/std/collections/hash_map/enum.Entry.html#method.or_insert")
CBM_STD_ENTRY_VALUE("std.collections.hash_map.Entry", "or_insert_with",
                    "https://doc.rust-lang.org/std/collections/hash_map/enum.Entry.html#method.or_insert_with")
CBM_STD_ENTRY_VALUE("std.collections.hash_map.Entry", "or_insert_with_key",
                    "https://doc.rust-lang.org/std/collections/hash_map/enum.Entry.html#method.or_insert_with_key")
CBM_STD_ENTRY_VALUE("std.collections.hash_map.Entry", "or_default",
                    "https://doc.rust-lang.org/std/collections/hash_map/enum.Entry.html#method.or_default")
CBM_STD_ENTRY_VALUE("std.collections.btree_map.Entry", "or_insert",
                    "https://doc.rust-lang.org/std/collections/btree_map/enum.Entry.html#method.or_insert")
CBM_STD_ENTRY_VALUE("std.collections.btree_map.Entry", "or_insert_with",
                    "https://doc.rust-lang.org/std/collections/btree_map/enum.Entry.html#method.or_insert_with")
CBM_STD_ENTRY_VALUE("std.collections.btree_map.Entry", "or_insert_with_key",
                    "https://doc.rust-lang.org/std/collections/btree_map/enum.Entry.html#method.or_insert_with_key")
CBM_STD_ENTRY_VALUE("std.collections.btree_map.Entry", "or_default",
                    "https://doc.rust-lang.org/std/collections/btree_map/enum.Entry.html#method.or_default")
CBM_STD_ENTRY_VALUE("hashbrown.hash_map.Entry", "or_insert",
                    "https://docs.rs/hashbrown/0.15/hashbrown/hash_map/enum.Entry.html#method.or_insert")
CBM_STD_ENTRY_VALUE("hashbrown.hash_map.Entry", "or_insert_with",
                    "https://docs.rs/hashbrown/0.15/hashbrown/hash_map/enum.Entry.html#method.or_insert_with")
CBM_STD_ENTRY_VALUE("hashbrown.hash_map.Entry", "or_insert_with_key",
                    "https://docs.rs/hashbrown/0.15/hashbrown/hash_map/enum.Entry.html#method.or_insert_with_key")
CBM_STD_ENTRY_VALUE("hashbrown.hash_map.Entry", "or_default",
                    "https://docs.rs/hashbrown/0.15/hashbrown/hash_map/enum.Entry.html#method.or_default")
#endif

#ifdef CBM_STD_INDEX_VALUE
CBM_STD_INDEX_VALUE("std.collections.HashMap",
                    "https://doc.rust-lang.org/std/collections/struct.HashMap.html#impl-Index%3C%26Q%3E-for-HashMap%3CK,+V,+S%3E")
CBM_STD_INDEX_VALUE("std.collections.BTreeMap",
                    "https://doc.rust-lang.org/std/collections/struct.BTreeMap.html#impl-Index%3C%26Q%3E-for-BTreeMap%3CK,+V,+A%3E")
#endif

#ifdef CBM_STD_ITER_ITEM
CBM_STD_ITER_ITEM("std.collections.hash_map.Iter", "&T,&E",
                  "https://doc.rust-lang.org/std/collections/hash_map/struct.Iter.html#impl-Iterator-for-Iter")
CBM_STD_ITER_ITEM("std.collections.hash_map.IterMut", "&T,!E",
                  "https://doc.rust-lang.org/std/collections/hash_map/struct.IterMut.html#impl-Iterator-for-IterMut")
CBM_STD_ITER_ITEM("std.collections.hash_map.IntoIter", "T,E",
                  "https://doc.rust-lang.org/std/collections/hash_map/struct.IntoIter.html#impl-Iterator-for-IntoIter")
CBM_STD_ITER_ITEM("std.collections.hash_map.Drain", "T,E",
                  "https://doc.rust-lang.org/std/collections/hash_map/struct.Drain.html#impl-Iterator-for-Drain")
CBM_STD_ITER_ITEM("std.collections.hash_map.Keys", "&T",
                  "https://doc.rust-lang.org/std/collections/hash_map/struct.Keys.html#impl-Iterator-for-Keys")
CBM_STD_ITER_ITEM("std.collections.hash_map.Values", "&E",
                  "https://doc.rust-lang.org/std/collections/hash_map/struct.Values.html#impl-Iterator-for-Values")
CBM_STD_ITER_ITEM("std.collections.hash_map.ValuesMut", "!E",
                  "https://doc.rust-lang.org/std/collections/hash_map/struct.ValuesMut.html#impl-Iterator-for-ValuesMut")
CBM_STD_ITER_ITEM("std.collections.hash_map.IntoKeys", "T",
                  "https://doc.rust-lang.org/std/collections/hash_map/struct.IntoKeys.html#impl-Iterator-for-IntoKeys")
CBM_STD_ITER_ITEM("std.collections.hash_map.IntoValues", "E",
                  "https://doc.rust-lang.org/std/collections/hash_map/struct.IntoValues.html#impl-Iterator-for-IntoValues")
CBM_STD_ITER_ITEM("std.collections.btree_map.Iter", "&T,&E",
                  "https://doc.rust-lang.org/std/collections/btree_map/struct.Iter.html#impl-Iterator-for-Iter")
CBM_STD_ITER_ITEM("std.collections.btree_map.IterMut", "&T,!E",
                  "https://doc.rust-lang.org/std/collections/btree_map/struct.IterMut.html#impl-Iterator-for-IterMut")
CBM_STD_ITER_ITEM("std.collections.btree_map.IntoIter", "T,E",
                  "https://doc.rust-lang.org/std/collections/btree_map/struct.IntoIter.html#impl-Iterator-for-IntoIter")
CBM_STD_ITER_ITEM("std.collections.btree_map.Keys", "&T",
                  "https://doc.rust-lang.org/std/collections/btree_map/struct.Keys.html#impl-Iterator-for-Keys")
CBM_STD_ITER_ITEM("std.collections.btree_map.Values", "&E",
                  "https://doc.rust-lang.org/std/collections/btree_map/struct.Values.html#impl-Iterator-for-Values")
CBM_STD_ITER_ITEM("std.collections.btree_map.ValuesMut", "!E",
                  "https://doc.rust-lang.org/std/collections/btree_map/struct.ValuesMut.html#impl-Iterator-for-ValuesMut")
CBM_STD_ITER_ITEM("std.collections.btree_map.IntoKeys", "T",
                  "https://doc.rust-lang.org/std/collections/btree_map/struct.IntoKeys.html#impl-Iterator-for-IntoKeys")
CBM_STD_ITER_ITEM("std.collections.btree_map.IntoValues", "E",
                  "https://doc.rust-lang.org/std/collections/btree_map/struct.IntoValues.html#impl-Iterator-for-IntoValues")
CBM_STD_ITER_ITEM("hashbrown.hash_map.Iter", "&T,&E",
                  "https://docs.rs/hashbrown/0.15/hashbrown/hash_map/struct.Iter.html#impl-Iterator-for-Iter")
CBM_STD_ITER_ITEM("hashbrown.hash_map.IterMut", "&T,!E",
                  "https://docs.rs/hashbrown/0.15/hashbrown/hash_map/struct.IterMut.html#impl-Iterator-for-IterMut")
CBM_STD_ITER_ITEM("hashbrown.hash_map.IntoIter", "T,E",
                  "https://docs.rs/hashbrown/0.15/hashbrown/hash_map/struct.IntoIter.html#impl-Iterator-for-IntoIter")
CBM_STD_ITER_ITEM("hashbrown.hash_map.Drain", "T,E",
                  "https://docs.rs/hashbrown/0.15/hashbrown/hash_map/struct.Drain.html#impl-Iterator-for-Drain")
CBM_STD_ITER_ITEM("hashbrown.hash_map.Keys", "&T",
                  "https://docs.rs/hashbrown/0.15/hashbrown/hash_map/struct.Keys.html#impl-Iterator-for-Keys")
CBM_STD_ITER_ITEM("hashbrown.hash_map.Values", "&E",
                  "https://docs.rs/hashbrown/0.15/hashbrown/hash_map/struct.Values.html#impl-Iterator-for-Values")
CBM_STD_ITER_ITEM("hashbrown.hash_map.ValuesMut", "!E",
                  "https://docs.rs/hashbrown/0.15/hashbrown/hash_map/struct.ValuesMut.html#impl-Iterator-for-ValuesMut")
CBM_STD_ITER_ITEM("hashbrown.hash_map.IntoKeys", "T",
                  "https://docs.rs/hashbrown/0.15/hashbrown/hash_map/struct.IntoKeys.html#impl-Iterator-for-IntoKeys")
CBM_STD_ITER_ITEM("hashbrown.hash_map.IntoValues", "E",
                  "https://docs.rs/hashbrown/0.15/hashbrown/hash_map/struct.IntoValues.html#impl-Iterator-for-IntoValues")
CBM_STD_ITER_ITEM("std.collections.hash_set.Iter", "&T",
                  "https://doc.rust-lang.org/std/collections/hash_set/struct.Iter.html#impl-Iterator-for-Iter")
CBM_STD_ITER_ITEM("std.collections.hash_set.IntoIter", "T",
                  "https://doc.rust-lang.org/std/collections/hash_set/struct.IntoIter.html#impl-Iterator-for-IntoIter")
CBM_STD_ITER_ITEM("std.collections.hash_set.Drain", "T",
                  "https://doc.rust-lang.org/std/collections/hash_set/struct.Drain.html#impl-Iterator-for-Drain")
CBM_STD_ITER_ITEM("std.collections.btree_set.Iter", "&T",
                  "https://doc.rust-lang.org/std/collections/btree_set/struct.Iter.html#impl-Iterator-for-Iter")
CBM_STD_ITER_ITEM("std.collections.btree_set.IntoIter", "T",
                  "https://doc.rust-lang.org/std/collections/btree_set/struct.IntoIter.html#impl-Iterator-for-IntoIter")
CBM_STD_ITER_ITEM("hashbrown.hash_set.Iter", "&T",
                  "https://docs.rs/hashbrown/0.15/hashbrown/hash_set/struct.Iter.html#impl-Iterator-for-Iter")
CBM_STD_ITER_ITEM("hashbrown.hash_set.IntoIter", "T",
                  "https://docs.rs/hashbrown/0.15/hashbrown/hash_set/struct.IntoIter.html#impl-Iterator-for-IntoIter")
CBM_STD_ITER_ITEM("hashbrown.hash_set.Drain", "T",
                  "https://docs.rs/hashbrown/0.15/hashbrown/hash_set/struct.Drain.html#impl-Iterator-for-Drain")
#endif

#ifdef CBM_STD_PRODUCER
CBM_STD_PRODUCER("mem", "take", "~A", "https://doc.rust-lang.org/std/mem/fn.take.html",
                 "2026-10-26")
CBM_STD_PRODUCER("mem", "replace", "~A", "https://doc.rust-lang.org/std/mem/fn.replace.html",
                 "2026-10-26")
#endif

#ifdef CBM_STD_SEQ_ELEM
CBM_STD_SEQ_ELEM("get_mut", "core.option.Option<!T>",
                 "https://doc.rust-lang.org/std/primitive.slice.html#method.get_mut")
#endif

#ifdef CBM_STD_GUARD_LEAF
CBM_STD_GUARD_LEAF("RwLockReadGuard", "std.sync.RwLockReadGuard",
                   "https://doc.rust-lang.org/std/sync/struct.RwLockReadGuard.html#impl-Deref-for-RwLockReadGuard%3C'_,+T%3E")
CBM_STD_GUARD_LEAF("RwLockWriteGuard", "std.sync.RwLockWriteGuard",
                   "https://doc.rust-lang.org/std/sync/struct.RwLockWriteGuard.html#impl-Deref-for-RwLockWriteGuard%3C'_,+T%3E")
CBM_STD_GUARD_LEAF("MutexGuard", "std.sync.MutexGuard",
                   "https://doc.rust-lang.org/std/sync/struct.MutexGuard.html#impl-Deref-for-MutexGuard%3C'_,+T%3E")
#endif
