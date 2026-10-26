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
 *   type argument. */

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
