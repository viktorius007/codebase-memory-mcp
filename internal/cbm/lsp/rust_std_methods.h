/* Frozen from rust_lsp.c at 61b613db; reviewed 2026-10-07.
 * Each row records receiver, method, the existing inference (not a link),
 * authoritative signature source and capture date. RS_* return syntax is
 * documented beside RustStdReturn. Legacy actions preserve the old rules,
 * including their substring matching and approximation; they do not certify
 * those approximations as std signatures. No index-time I/O is needed. */

#ifdef CBM_STD_RETURN
CBM_STD_RETURN(RUST_STD_OPTION, "unwrap", "T", false,
               "https://doc.rust-lang.org/std/option/enum.Option.html#method.unwrap", "2026-10-07")
CBM_STD_RETURN(RUST_STD_OPTION, "expect", "T", false,
               "https://doc.rust-lang.org/std/option/enum.Option.html#method.expect", "2026-10-07")
CBM_STD_RETURN(RUST_STD_OPTION, "unwrap_or", "T", false,
               "https://doc.rust-lang.org/std/option/enum.Option.html#method.unwrap_or",
               "2026-10-07")
CBM_STD_RETURN(RUST_STD_OPTION, "unwrap_or_else", "T", false,
               "https://doc.rust-lang.org/std/option/enum.Option.html#method.unwrap_or_else",
               "2026-10-07")
CBM_STD_RETURN(RUST_STD_OPTION, "unwrap_or_default", "T", false,
               "https://doc.rust-lang.org/std/option/enum.Option.html#method.unwrap_or_default",
               "2026-10-07")
CBM_STD_RETURN(RUST_STD_OPTION, "unwrap_unchecked", "T", false,
               "https://doc.rust-lang.org/std/option/enum.Option.html#method.unwrap_unchecked",
               "2026-10-07")
CBM_STD_RETURN(RUST_STD_OPTION, "as_ref", RS_OPT("&T"), false,
               "https://doc.rust-lang.org/std/option/enum.Option.html#method.as_ref", "2026-10-07")
CBM_STD_RETURN(RUST_STD_OPTION, "as_mut", RS_OPT("!T"), false,
               "https://doc.rust-lang.org/std/option/enum.Option.html#method.as_mut", "2026-10-07")
CBM_STD_RETURN(RUST_STD_OPTION, "take", "Self", false,
               "https://doc.rust-lang.org/std/option/enum.Option.html#method.take", "2026-10-07")
CBM_STD_RETURN(RUST_STD_OPTION, "take_if", "Self", false,
               "https://doc.rust-lang.org/std/option/enum.Option.html#method.take_if", "2026-10-07")
CBM_STD_RETURN(RUST_STD_OPTION, "replace", "Self", false,
               "https://doc.rust-lang.org/std/option/enum.Option.html#method.replace", "2026-10-07")
CBM_STD_RETURN(RUST_STD_OPTION, "clone", "Self", false,
               "https://doc.rust-lang.org/std/option/enum.Option.html#method.clone", "2026-10-07")
CBM_STD_RETURN(RUST_STD_OPTION, "or", "Self", false,
               "https://doc.rust-lang.org/std/option/enum.Option.html#method.or", "2026-10-07")
CBM_STD_RETURN(RUST_STD_OPTION, "or_else", "Self", false,
               "https://doc.rust-lang.org/std/option/enum.Option.html#method.or_else", "2026-10-07")
CBM_STD_RETURN(RUST_STD_OPTION, "xor", "Self", false,
               "https://doc.rust-lang.org/std/option/enum.Option.html#method.xor", "2026-10-07")
CBM_STD_RETURN(RUST_STD_OPTION, "filter", "Self", false,
               "https://doc.rust-lang.org/std/option/enum.Option.html#method.filter", "2026-10-07")
CBM_STD_RETURN(RUST_STD_OPTION, "inspect", "Self", false,
               "https://doc.rust-lang.org/std/option/enum.Option.html#method.inspect", "2026-10-07")
CBM_STD_RETURN(RUST_STD_OPTION, "cloned", RS_OPT("~T"), false,
               "https://doc.rust-lang.org/std/option/enum.Option.html#method.cloned", "2026-10-07")
CBM_STD_RETURN(RUST_STD_OPTION, "copied", RS_OPT("~T"), false,
               "https://doc.rust-lang.org/std/option/enum.Option.html#method.copied", "2026-10-07")
CBM_STD_RETURN(RUST_STD_OPTION, "ok_or", RS_RES("T", "_"), false,
               "https://doc.rust-lang.org/std/option/enum.Option.html#method.ok_or", "2026-10-07")
CBM_STD_RETURN(RUST_STD_OPTION, "ok_or_else", RS_RES("T", "_"), false,
               "https://doc.rust-lang.org/std/option/enum.Option.html#method.ok_or_else",
               "2026-10-07")
CBM_STD_RETURN(RUST_STD_OPTION, "is_some", "bool", false,
               "https://doc.rust-lang.org/std/option/enum.Option.html#method.is_some", "2026-10-07")
CBM_STD_RETURN(RUST_STD_OPTION, "is_none", "bool", false,
               "https://doc.rust-lang.org/std/option/enum.Option.html#method.is_none", "2026-10-07")
CBM_STD_RETURN(RUST_STD_OPTION, "is_some_and", "bool", false,
               "https://doc.rust-lang.org/std/option/enum.Option.html#method.is_some_and",
               "2026-10-07")
CBM_STD_RETURN(RUST_STD_OPTION, "is_none_or", "bool", false,
               "https://doc.rust-lang.org/std/option/enum.Option.html#method.is_none_or",
               "2026-10-07")
CBM_STD_RETURN(RUST_STD_OPTION, "insert", "&T", false,
               "https://doc.rust-lang.org/std/option/enum.Option.html#method.insert", "2026-10-07")
CBM_STD_RETURN(RUST_STD_OPTION, "get_or_insert", "&T", false,
               "https://doc.rust-lang.org/std/option/enum.Option.html#method.get_or_insert",
               "2026-10-07")
CBM_STD_RETURN(RUST_STD_OPTION, "get_or_insert_with", "&T", false,
               "https://doc.rust-lang.org/std/option/enum.Option.html#method.get_or_insert_with",
               "2026-10-07")
CBM_STD_RETURN(RUST_STD_OPTION, "get_or_insert_default", "&T", false,
               "https://doc.rust-lang.org/std/option/enum.Option.html#method.get_or_insert_default",
               "2026-10-07")
CBM_STD_RETURN(RUST_STD_OPTION, "as_deref", RS_OPT("&*T"), false,
               "https://doc.rust-lang.org/std/option/enum.Option.html#method.as_deref",
               "2026-10-07")
CBM_STD_RETURN(RUST_STD_OPTION, "as_deref_mut", RS_OPT("!*T"), false,
               "https://doc.rust-lang.org/std/option/enum.Option.html#method.as_deref_mut",
               "2026-10-07")
CBM_STD_RETURN(RUST_STD_OPTION, "flatten", "T", false,
               "https://doc.rust-lang.org/std/option/enum.Option.html#method.flatten", "2026-10-07")
CBM_STD_RETURN(RUST_STD_OPTION, "map", RS_OPT("F"), false,
               "https://doc.rust-lang.org/std/option/enum.Option.html#method.map", "2026-10-07")
CBM_STD_RETURN(RUST_STD_OPTION, "and_then", RS_OPT("?F"), false,
               "https://doc.rust-lang.org/std/option/enum.Option.html#method.and_then",
               "2026-10-07")
CBM_STD_RETURN(RUST_STD_OPTION, "map_or", "A", false,
               "https://doc.rust-lang.org/std/option/enum.Option.html#method.map_or", "2026-10-07")
CBM_STD_RETURN(RUST_STD_OPTION, "map_or_else", "F", false,
               "https://doc.rust-lang.org/std/option/enum.Option.html#method.map_or_else",
               "2026-10-07")
CBM_STD_RETURN(RUST_STD_OPTION, "iter", RS_ITER("&T"), false,
               "https://doc.rust-lang.org/std/option/enum.Option.html#method.iter", "2026-10-07")
CBM_STD_RETURN(RUST_STD_OPTION, "iter_mut", RS_ITER("!T"), false,
               "https://doc.rust-lang.org/std/option/enum.Option.html#method.iter_mut",
               "2026-10-07")
CBM_STD_RETURN(RUST_STD_OPTION, "into_iter", RS_ITER("T"), false,
               "https://doc.rust-lang.org/std/option/enum.Option.html#method.into_iter",
               "2026-10-07")
CBM_STD_RETURN(RUST_STD_RESULT, "unwrap", "T", false,
               "https://doc.rust-lang.org/std/result/enum.Result.html#method.unwrap", "2026-10-07")
CBM_STD_RETURN(RUST_STD_RESULT, "expect", "T", false,
               "https://doc.rust-lang.org/std/result/enum.Result.html#method.expect", "2026-10-07")
CBM_STD_RETURN(RUST_STD_RESULT, "unwrap_or", "T", false,
               "https://doc.rust-lang.org/std/result/enum.Result.html#method.unwrap_or",
               "2026-10-07")
CBM_STD_RETURN(RUST_STD_RESULT, "unwrap_or_else", "T", false,
               "https://doc.rust-lang.org/std/result/enum.Result.html#method.unwrap_or_else",
               "2026-10-07")
CBM_STD_RETURN(RUST_STD_RESULT, "unwrap_or_default", "T", false,
               "https://doc.rust-lang.org/std/result/enum.Result.html#method.unwrap_or_default",
               "2026-10-07")
CBM_STD_RETURN(RUST_STD_RESULT, "unwrap_err", "E", false,
               "https://doc.rust-lang.org/std/result/enum.Result.html#method.unwrap_err",
               "2026-10-07")
CBM_STD_RETURN(RUST_STD_RESULT, "expect_err", "E", false,
               "https://doc.rust-lang.org/std/result/enum.Result.html#method.expect_err",
               "2026-10-07")
CBM_STD_RETURN(RUST_STD_RESULT, "ok", RS_OPT("T"), false,
               "https://doc.rust-lang.org/std/result/enum.Result.html#method.ok", "2026-10-07")
CBM_STD_RETURN(RUST_STD_RESULT, "err", RS_OPT("E"), false,
               "https://doc.rust-lang.org/std/result/enum.Result.html#method.err", "2026-10-07")
CBM_STD_RETURN(RUST_STD_RESULT, "map_err", RS_RES("T", "_"), false,
               "https://doc.rust-lang.org/std/result/enum.Result.html#method.map_err", "2026-10-07")
CBM_STD_RETURN(RUST_STD_RESULT, "or", RS_RES("T", "_"), false,
               "https://doc.rust-lang.org/std/result/enum.Result.html#method.or", "2026-10-07")
CBM_STD_RETURN(RUST_STD_RESULT, "or_else", RS_RES("T", "_"), false,
               "https://doc.rust-lang.org/std/result/enum.Result.html#method.or_else", "2026-10-07")
CBM_STD_RETURN(RUST_STD_RESULT, "map", RS_RES("F", "E"), false,
               "https://doc.rust-lang.org/std/result/enum.Result.html#method.map", "2026-10-07")
CBM_STD_RETURN(RUST_STD_RESULT, "and", RS_RES("_", "E"), false,
               "https://doc.rust-lang.org/std/result/enum.Result.html#method.and", "2026-10-07")
CBM_STD_RETURN(RUST_STD_RESULT, "and_then", RS_RES("?F", "E"), false,
               "https://doc.rust-lang.org/std/result/enum.Result.html#method.and_then",
               "2026-10-07")
CBM_STD_RETURN(RUST_STD_RESULT, "as_deref", RS_RES("&*T", "&E"), false,
               "https://doc.rust-lang.org/std/result/enum.Result.html#method.as_deref",
               "2026-10-07")
CBM_STD_RETURN(RUST_STD_RESULT, "as_deref_mut", RS_RES("!*T", "!E"), false,
               "https://doc.rust-lang.org/std/result/enum.Result.html#method.as_deref_mut",
               "2026-10-07")
CBM_STD_RETURN(RUST_STD_RESULT, "map_or", "A", false,
               "https://doc.rust-lang.org/std/result/enum.Result.html#method.map_or", "2026-10-07")
CBM_STD_RETURN(RUST_STD_RESULT, "as_ref", RS_RES("&T", "&E"), false,
               "https://doc.rust-lang.org/std/result/enum.Result.html#method.as_ref", "2026-10-07")
CBM_STD_RETURN(RUST_STD_RESULT, "as_mut", RS_RES("!T", "!E"), false,
               "https://doc.rust-lang.org/std/result/enum.Result.html#method.as_mut", "2026-10-07")
CBM_STD_RETURN(RUST_STD_RESULT, "cloned", RS_RES("~T", "E"), false,
               "https://doc.rust-lang.org/std/result/enum.Result.html#method.cloned", "2026-10-07")
CBM_STD_RETURN(RUST_STD_RESULT, "copied", RS_RES("~T", "E"), false,
               "https://doc.rust-lang.org/std/result/enum.Result.html#method.copied", "2026-10-07")
CBM_STD_RETURN(RUST_STD_RESULT, "clone", "Self", false,
               "https://doc.rust-lang.org/std/result/enum.Result.html#method.clone", "2026-10-07")
CBM_STD_RETURN(RUST_STD_RESULT, "inspect", "Self", false,
               "https://doc.rust-lang.org/std/result/enum.Result.html#method.inspect", "2026-10-07")
CBM_STD_RETURN(RUST_STD_RESULT, "inspect_err", "Self", false,
               "https://doc.rust-lang.org/std/result/enum.Result.html#method.inspect_err",
               "2026-10-07")
CBM_STD_RETURN(RUST_STD_RESULT, "is_ok", "bool", false,
               "https://doc.rust-lang.org/std/result/enum.Result.html#method.is_ok", "2026-10-07")
CBM_STD_RETURN(RUST_STD_RESULT, "is_err", "bool", false,
               "https://doc.rust-lang.org/std/result/enum.Result.html#method.is_err", "2026-10-07")
CBM_STD_RETURN(RUST_STD_RESULT, "is_ok_and", "bool", false,
               "https://doc.rust-lang.org/std/result/enum.Result.html#method.is_ok_and",
               "2026-10-07")
CBM_STD_RETURN(RUST_STD_RESULT, "is_err_and", "bool", false,
               "https://doc.rust-lang.org/std/result/enum.Result.html#method.is_err_and",
               "2026-10-07")
CBM_STD_RETURN(RUST_STD_ARC, "clone", "Self", false,
               "https://doc.rust-lang.org/std/sync/struct.Arc.html#method.clone", "2026-10-07")
CBM_STD_RETURN(RUST_STD_ARC, "as_ref", "&T", false,
               "https://doc.rust-lang.org/std/sync/struct.Arc.html#method.as_ref", "2026-10-07")
CBM_STD_RETURN(RUST_STD_ARC, "downgrade", "alloc.sync.Weak<T>", true,
               "https://doc.rust-lang.org/std/sync/struct.Arc.html#method.downgrade", "2026-10-07")
CBM_STD_RETURN(RUST_STD_ARC, "strong_count", "usize", true,
               "https://doc.rust-lang.org/std/sync/struct.Arc.html#method.strong_count",
               "2026-10-07")
CBM_STD_RETURN(RUST_STD_ARC, "weak_count", "usize", true,
               "https://doc.rust-lang.org/std/sync/struct.Arc.html#method.weak_count", "2026-10-07")
CBM_STD_RETURN(RUST_STD_ARC, "ptr_eq", "bool", true,
               "https://doc.rust-lang.org/std/sync/struct.Arc.html#method.ptr_eq", "2026-10-07")
CBM_STD_RETURN(RUST_STD_ARC, "try_unwrap", RS_RES("T", "Self"), true,
               "https://doc.rust-lang.org/std/sync/struct.Arc.html#method.try_unwrap", "2026-10-07")
CBM_STD_RETURN(RUST_STD_ARC, "into_inner", RS_OPT("T"), true,
               "https://doc.rust-lang.org/std/sync/struct.Arc.html#method.into_inner", "2026-10-07")
CBM_STD_RETURN(RUST_STD_ARC, "unwrap_or_clone", "T", true,
               "https://doc.rust-lang.org/std/sync/struct.Arc.html#method.unwrap_or_clone",
               "2026-10-07")
CBM_STD_RETURN(RUST_STD_ARC, "get_mut", RS_OPT("!T"), true,
               "https://doc.rust-lang.org/std/sync/struct.Arc.html#method.get_mut", "2026-10-07")
CBM_STD_RETURN(RUST_STD_ARC, "make_mut", "!T", true,
               "https://doc.rust-lang.org/std/sync/struct.Arc.html#method.make_mut", "2026-10-07")
CBM_STD_RETURN(RUST_STD_RC, "clone", "Self", false,
               "https://doc.rust-lang.org/std/rc/struct.Rc.html#method.clone", "2026-10-07")
CBM_STD_RETURN(RUST_STD_RC, "as_ref", "&T", false,
               "https://doc.rust-lang.org/std/rc/struct.Rc.html#method.as_ref", "2026-10-07")
CBM_STD_RETURN(RUST_STD_RC, "downgrade", "alloc.rc.Weak<T>", true,
               "https://doc.rust-lang.org/std/rc/struct.Rc.html#method.downgrade", "2026-10-07")
CBM_STD_RETURN(RUST_STD_RC, "strong_count", "usize", true,
               "https://doc.rust-lang.org/std/rc/struct.Rc.html#method.strong_count", "2026-10-07")
CBM_STD_RETURN(RUST_STD_RC, "weak_count", "usize", true,
               "https://doc.rust-lang.org/std/rc/struct.Rc.html#method.weak_count", "2026-10-07")
CBM_STD_RETURN(RUST_STD_RC, "ptr_eq", "bool", true,
               "https://doc.rust-lang.org/std/rc/struct.Rc.html#method.ptr_eq", "2026-10-07")
CBM_STD_RETURN(RUST_STD_RC, "try_unwrap", RS_RES("T", "Self"), true,
               "https://doc.rust-lang.org/std/rc/struct.Rc.html#method.try_unwrap", "2026-10-07")
CBM_STD_RETURN(RUST_STD_RC, "into_inner", RS_OPT("T"), true,
               "https://doc.rust-lang.org/std/rc/struct.Rc.html#method.into_inner", "2026-10-07")
CBM_STD_RETURN(RUST_STD_RC, "unwrap_or_clone", "T", true,
               "https://doc.rust-lang.org/std/rc/struct.Rc.html#method.unwrap_or_clone",
               "2026-10-07")
CBM_STD_RETURN(RUST_STD_RC, "get_mut", RS_OPT("!T"), true,
               "https://doc.rust-lang.org/std/rc/struct.Rc.html#method.get_mut", "2026-10-07")
CBM_STD_RETURN(RUST_STD_RC, "make_mut", "!T", true,
               "https://doc.rust-lang.org/std/rc/struct.Rc.html#method.make_mut", "2026-10-07")
CBM_STD_RETURN(RUST_STD_WEAK_SYNC, "clone", "Self", false,
               "https://doc.rust-lang.org/std/sync/struct.Weak.html#method.clone", "2026-10-07")
CBM_STD_RETURN(RUST_STD_WEAK_SYNC, "upgrade", RS_OPT("alloc.sync.Arc<T>"), false,
               "https://doc.rust-lang.org/std/sync/struct.Weak.html#method.upgrade", "2026-10-07")
CBM_STD_RETURN(RUST_STD_WEAK_SYNC, "strong_count", "usize", false,
               "https://doc.rust-lang.org/std/sync/struct.Weak.html#method.strong_count",
               "2026-10-07")
CBM_STD_RETURN(RUST_STD_WEAK_SYNC, "weak_count", "usize", false,
               "https://doc.rust-lang.org/std/sync/struct.Weak.html#method.weak_count",
               "2026-10-07")
CBM_STD_RETURN(RUST_STD_WEAK_SYNC, "ptr_eq", "bool", false,
               "https://doc.rust-lang.org/std/sync/struct.Weak.html#method.ptr_eq", "2026-10-07")
CBM_STD_RETURN(RUST_STD_WEAK_RC, "clone", "Self", false,
               "https://doc.rust-lang.org/std/rc/struct.Weak.html#method.clone", "2026-10-07")
CBM_STD_RETURN(RUST_STD_WEAK_RC, "upgrade", RS_OPT("alloc.rc.Rc<T>"), false,
               "https://doc.rust-lang.org/std/rc/struct.Weak.html#method.upgrade", "2026-10-07")
CBM_STD_RETURN(RUST_STD_WEAK_RC, "strong_count", "usize", false,
               "https://doc.rust-lang.org/std/rc/struct.Weak.html#method.strong_count",
               "2026-10-07")
CBM_STD_RETURN(RUST_STD_WEAK_RC, "weak_count", "usize", false,
               "https://doc.rust-lang.org/std/rc/struct.Weak.html#method.weak_count", "2026-10-07")
CBM_STD_RETURN(RUST_STD_WEAK_RC, "ptr_eq", "bool", false,
               "https://doc.rust-lang.org/std/rc/struct.Weak.html#method.ptr_eq", "2026-10-07")
CBM_STD_RETURN(RUST_STD_MUTEX, "lock", RS_RES("std.sync.MutexGuard<T>", "_"), false,
               "https://doc.rust-lang.org/std/sync/struct.Mutex.html#method.lock", "2026-10-07")
CBM_STD_RETURN(RUST_STD_MUTEX, "try_lock", RS_RES("std.sync.MutexGuard<T>", "_"), false,
               "https://doc.rust-lang.org/std/sync/struct.Mutex.html#method.try_lock", "2026-10-07")
CBM_STD_RETURN(RUST_STD_MUTEX, "into_inner", RS_RES("T", "_"), false,
               "https://doc.rust-lang.org/std/sync/struct.Mutex.html#method.into_inner",
               "2026-10-07")
CBM_STD_RETURN(RUST_STD_MUTEX, "get_mut", RS_RES("!T", "_"), false,
               "https://doc.rust-lang.org/std/sync/struct.Mutex.html#method.get_mut", "2026-10-07")
CBM_STD_RETURN(RUST_STD_MUTEX, "is_poisoned", "bool", false,
               "https://doc.rust-lang.org/std/sync/struct.Mutex.html#method.is_poisoned",
               "2026-10-07")
CBM_STD_RETURN(RUST_STD_MUTEX, "clear_poison", "()", false,
               "https://doc.rust-lang.org/std/sync/struct.Mutex.html#method.clear_poison",
               "2026-10-07")
CBM_STD_RETURN(RUST_STD_RWLOCK, "read", RS_RES("std.sync.RwLockReadGuard<T>", "_"), false,
               "https://doc.rust-lang.org/std/sync/struct.RwLock.html#method.read", "2026-10-07")
CBM_STD_RETURN(RUST_STD_RWLOCK, "try_read", RS_RES("std.sync.RwLockReadGuard<T>", "_"), false,
               "https://doc.rust-lang.org/std/sync/struct.RwLock.html#method.try_read",
               "2026-10-07")
CBM_STD_RETURN(RUST_STD_RWLOCK, "write", RS_RES("std.sync.RwLockWriteGuard<T>", "_"), false,
               "https://doc.rust-lang.org/std/sync/struct.RwLock.html#method.write", "2026-10-07")
CBM_STD_RETURN(RUST_STD_RWLOCK, "try_write", RS_RES("std.sync.RwLockWriteGuard<T>", "_"), false,
               "https://doc.rust-lang.org/std/sync/struct.RwLock.html#method.try_write",
               "2026-10-07")
CBM_STD_RETURN(RUST_STD_RWLOCK, "into_inner", RS_RES("T", "_"), false,
               "https://doc.rust-lang.org/std/sync/struct.RwLock.html#method.into_inner",
               "2026-10-07")
CBM_STD_RETURN(RUST_STD_RWLOCK, "get_mut", RS_RES("!T", "_"), false,
               "https://doc.rust-lang.org/std/sync/struct.RwLock.html#method.get_mut", "2026-10-07")
CBM_STD_RETURN(RUST_STD_RWLOCK, "is_poisoned", "bool", false,
               "https://doc.rust-lang.org/std/sync/struct.RwLock.html#method.is_poisoned",
               "2026-10-07")
CBM_STD_RETURN(RUST_STD_RWLOCK, "clear_poison", "()", false,
               "https://doc.rust-lang.org/std/sync/struct.RwLock.html#method.clear_poison",
               "2026-10-07")
CBM_STD_RETURN(RUST_STD_REFCELL, "borrow", "core.cell.Ref<T>", false,
               "https://doc.rust-lang.org/std/cell/struct.RefCell.html#method.borrow", "2026-10-07")
CBM_STD_RETURN(RUST_STD_REFCELL, "borrow_mut", "core.cell.RefMut<T>", false,
               "https://doc.rust-lang.org/std/cell/struct.RefCell.html#method.borrow_mut",
               "2026-10-07")
CBM_STD_RETURN(RUST_STD_REFCELL, "try_borrow", RS_RES("core.cell.Ref<T>", "_"), false,
               "https://doc.rust-lang.org/std/cell/struct.RefCell.html#method.try_borrow",
               "2026-10-07")
CBM_STD_RETURN(RUST_STD_REFCELL, "try_borrow_mut", RS_RES("core.cell.RefMut<T>", "_"), false,
               "https://doc.rust-lang.org/std/cell/struct.RefCell.html#method.try_borrow_mut",
               "2026-10-07")
CBM_STD_RETURN(RUST_STD_REFCELL, "into_inner", "T", false,
               "https://doc.rust-lang.org/std/cell/struct.RefCell.html#method.into_inner",
               "2026-10-07")
CBM_STD_RETURN(RUST_STD_REFCELL, "get_mut", "!T", false,
               "https://doc.rust-lang.org/std/cell/struct.RefCell.html#method.get_mut",
               "2026-10-07")
CBM_STD_RETURN(RUST_STD_REFCELL, "take", "T", false,
               "https://doc.rust-lang.org/std/cell/struct.RefCell.html#method.take", "2026-10-07")
CBM_STD_RETURN(RUST_STD_REFCELL, "replace", "T", false,
               "https://doc.rust-lang.org/std/cell/struct.RefCell.html#method.replace",
               "2026-10-07")
CBM_STD_RETURN(RUST_STD_REFCELL, "replace_with", "T", false,
               "https://doc.rust-lang.org/std/cell/struct.RefCell.html#method.replace_with",
               "2026-10-07")
CBM_STD_RETURN(RUST_STD_REFCELL, "clone", "Self", false,
               "https://doc.rust-lang.org/std/cell/struct.RefCell.html#method.clone", "2026-10-07")
CBM_STD_RETURN(RUST_STD_NONNULL, "as_ref", "&T", false,
               "https://doc.rust-lang.org/std/ptr/struct.NonNull.html#method.as_ref", "2026-10-26")
CBM_STD_RETURN(RUST_STD_NONNULL, "as_mut", "!T", false,
               "https://doc.rust-lang.org/std/ptr/struct.NonNull.html#method.as_mut", "2026-10-26")
CBM_STD_RETURN(RUST_STD_CELL, "get", "T", false,
               "https://doc.rust-lang.org/std/cell/struct.Cell.html#method.get", "2026-10-07")
CBM_STD_RETURN(RUST_STD_CELL, "take", "T", false,
               "https://doc.rust-lang.org/std/cell/struct.Cell.html#method.take", "2026-10-07")
CBM_STD_RETURN(RUST_STD_CELL, "replace", "T", false,
               "https://doc.rust-lang.org/std/cell/struct.Cell.html#method.replace", "2026-10-07")
CBM_STD_RETURN(RUST_STD_CELL, "into_inner", "T", false,
               "https://doc.rust-lang.org/std/cell/struct.Cell.html#method.into_inner",
               "2026-10-07")
CBM_STD_RETURN(RUST_STD_CELL, "get_mut", "!T", false,
               "https://doc.rust-lang.org/std/cell/struct.Cell.html#method.get_mut", "2026-10-07")
CBM_STD_RETURN(RUST_STD_CELL, "set", "()", false,
               "https://doc.rust-lang.org/std/cell/struct.Cell.html#method.set", "2026-10-07")
CBM_STD_RETURN(RUST_STD_PATHBUF, "as_path", "&" RS_PATH, false,
               "https://doc.rust-lang.org/std/path/struct.PathBuf.html#method.as_path",
               "2026-10-07")
CBM_STD_RETURN(RUST_STD_PATHBUF, "clone", "Self", false,
               "https://doc.rust-lang.org/std/path/struct.PathBuf.html#method.clone", "2026-10-07")
CBM_STD_RETURN(RUST_STD_PATHBUF, "to_owned", "Self", false,
               "https://doc.rust-lang.org/std/path/struct.PathBuf.html#method.to_owned",
               "2026-10-07")
CBM_STD_RETURN(RUST_STD_PATHBUF, "push", "()", false,
               "https://doc.rust-lang.org/std/path/struct.PathBuf.html#method.push", "2026-10-07")
CBM_STD_RETURN(RUST_STD_PATHBUF, "pop", "bool", false,
               "https://doc.rust-lang.org/std/path/struct.PathBuf.html#method.pop", "2026-10-07")
CBM_STD_RETURN(RUST_STD_PATHBUF, "set_extension", "bool", false,
               "https://doc.rust-lang.org/std/path/struct.PathBuf.html#method.set_extension",
               "2026-10-07")
CBM_STD_RETURN(RUST_STD_PATHBUF, "set_file_name", "()", false,
               "https://doc.rust-lang.org/std/path/struct.PathBuf.html#method.set_file_name",
               "2026-10-07")
CBM_STD_RETURN(RUST_STD_PATHBUF, "capacity", "usize", false,
               "https://doc.rust-lang.org/std/path/struct.PathBuf.html#method.capacity",
               "2026-10-07")
CBM_STD_RETURN(RUST_STD_PATHBUF, "into_os_string", "std.ffi.OsString", false,
               "https://doc.rust-lang.org/std/path/struct.PathBuf.html#method.into_os_string",
               "2026-10-07")
CBM_STD_RETURN(RUST_STD_PATHBUF, "into_boxed_path", "alloc.boxed.Box<" RS_PATH ">", false,
               "https://doc.rust-lang.org/std/path/struct.PathBuf.html#method.into_boxed_path",
               "2026-10-07")
CBM_STD_RETURN(RUST_STD_PATH, "join", RS_PATHBUF, false,
               "https://doc.rust-lang.org/std/path/struct.Path.html#method.join", "2026-10-07")
CBM_STD_RETURN(RUST_STD_PATH, "to_path_buf", RS_PATHBUF, false,
               "https://doc.rust-lang.org/std/path/struct.Path.html#method.to_path_buf",
               "2026-10-07")
CBM_STD_RETURN(RUST_STD_PATH, "to_owned", RS_PATHBUF, false,
               "https://doc.rust-lang.org/std/path/struct.Path.html#method.to_owned", "2026-10-07")
CBM_STD_RETURN(RUST_STD_PATH, "with_extension", RS_PATHBUF, false,
               "https://doc.rust-lang.org/std/path/struct.Path.html#method.with_extension",
               "2026-10-07")
CBM_STD_RETURN(RUST_STD_PATH, "with_file_name", RS_PATHBUF, false,
               "https://doc.rust-lang.org/std/path/struct.Path.html#method.with_file_name",
               "2026-10-07")
CBM_STD_RETURN(RUST_STD_PATH, "parent", RS_OPT("&" RS_PATH), false,
               "https://doc.rust-lang.org/std/path/struct.Path.html#method.parent", "2026-10-07")
CBM_STD_RETURN(RUST_STD_PATH, "file_name", RS_OPT("&" RS_OSSTR), false,
               "https://doc.rust-lang.org/std/path/struct.Path.html#method.file_name", "2026-10-07")
CBM_STD_RETURN(RUST_STD_PATH, "file_stem", RS_OPT("&" RS_OSSTR), false,
               "https://doc.rust-lang.org/std/path/struct.Path.html#method.file_stem", "2026-10-07")
CBM_STD_RETURN(RUST_STD_PATH, "extension", RS_OPT("&" RS_OSSTR), false,
               "https://doc.rust-lang.org/std/path/struct.Path.html#method.extension", "2026-10-07")
CBM_STD_RETURN(RUST_STD_PATH, "as_os_str", "&" RS_OSSTR, false,
               "https://doc.rust-lang.org/std/path/struct.Path.html#method.as_os_str", "2026-10-07")
CBM_STD_RETURN(RUST_STD_PATH, "to_str", RS_OPT("&str"), false,
               "https://doc.rust-lang.org/std/path/struct.Path.html#method.to_str", "2026-10-07")
CBM_STD_RETURN(RUST_STD_PATH, "strip_prefix", RS_RES("&" RS_PATH, "_"), false,
               "https://doc.rust-lang.org/std/path/struct.Path.html#method.strip_prefix",
               "2026-10-07")
CBM_STD_RETURN(RUST_STD_PATH, "canonicalize", RS_RES(RS_PATHBUF, "_"), false,
               "https://doc.rust-lang.org/std/path/struct.Path.html#method.canonicalize",
               "2026-10-07")
CBM_STD_RETURN(RUST_STD_PATH, "read_link", RS_RES(RS_PATHBUF, "_"), false,
               "https://doc.rust-lang.org/std/path/struct.Path.html#method.read_link", "2026-10-07")
CBM_STD_RETURN(RUST_STD_PATH, "display", "std.path.Display", false,
               "https://doc.rust-lang.org/std/path/struct.Path.html#method.display", "2026-10-07")
CBM_STD_RETURN(RUST_STD_PATH, "exists", "bool", false,
               "https://doc.rust-lang.org/std/path/struct.Path.html#method.exists", "2026-10-07")
CBM_STD_RETURN(RUST_STD_PATH, "is_file", "bool", false,
               "https://doc.rust-lang.org/std/path/struct.Path.html#method.is_file", "2026-10-07")
CBM_STD_RETURN(RUST_STD_PATH, "is_dir", "bool", false,
               "https://doc.rust-lang.org/std/path/struct.Path.html#method.is_dir", "2026-10-07")
CBM_STD_RETURN(RUST_STD_PATH, "is_symlink", "bool", false,
               "https://doc.rust-lang.org/std/path/struct.Path.html#method.is_symlink",
               "2026-10-07")
CBM_STD_RETURN(RUST_STD_PATH, "is_absolute", "bool", false,
               "https://doc.rust-lang.org/std/path/struct.Path.html#method.is_absolute",
               "2026-10-07")
CBM_STD_RETURN(RUST_STD_PATH, "is_relative", "bool", false,
               "https://doc.rust-lang.org/std/path/struct.Path.html#method.is_relative",
               "2026-10-07")
CBM_STD_RETURN(RUST_STD_PATH, "has_root", "bool", false,
               "https://doc.rust-lang.org/std/path/struct.Path.html#method.has_root", "2026-10-07")
CBM_STD_RETURN(RUST_STD_PATH, "starts_with", "bool", false,
               "https://doc.rust-lang.org/std/path/struct.Path.html#method.starts_with",
               "2026-10-07")
CBM_STD_RETURN(RUST_STD_PATH, "ends_with", "bool", false,
               "https://doc.rust-lang.org/std/path/struct.Path.html#method.ends_with", "2026-10-07")
CBM_STD_RETURN(RUST_STD_PATH, "ancestors", RS_ITER("&" RS_PATH), false,
               "https://doc.rust-lang.org/std/path/struct.Path.html#method.ancestors", "2026-10-07")
CBM_STD_RETURN(RUST_STD_STRING, "clone", "Self", false,
               "https://doc.rust-lang.org/std/string/struct.String.html#method.clone", "2026-10-07")
CBM_STD_RETURN(RUST_STD_STRING, "to_owned", "Self", false,
               "https://doc.rust-lang.org/std/string/struct.String.html#method.to_owned",
               "2026-10-07")
CBM_STD_RETURN(RUST_STD_STRING, "to_string", "Self", false,
               "https://doc.rust-lang.org/std/string/struct.String.html#method.to_string",
               "2026-10-07")
CBM_STD_RETURN(RUST_STD_STRING, "to_lowercase", "Self", false,
               "https://doc.rust-lang.org/std/string/struct.String.html#method.to_lowercase",
               "2026-10-07")
CBM_STD_RETURN(RUST_STD_STRING, "to_uppercase", "Self", false,
               "https://doc.rust-lang.org/std/string/struct.String.html#method.to_uppercase",
               "2026-10-07")
CBM_STD_RETURN(RUST_STD_STRING, "repeat", "Self", false,
               "https://doc.rust-lang.org/std/string/struct.String.html#method.repeat",
               "2026-10-07")
CBM_STD_RETURN(RUST_STD_STRING, "replace", "Self", false,
               "https://doc.rust-lang.org/std/string/struct.String.html#method.replace",
               "2026-10-07")
CBM_STD_RETURN(RUST_STD_STRING, "split_off", "Self", false,
               "https://doc.rust-lang.org/std/string/struct.String.html#method.split_off",
               "2026-10-07")
CBM_STD_RETURN(RUST_STD_STRING, "as_str", "&str", false,
               "https://doc.rust-lang.org/std/string/struct.String.html#method.as_str",
               "2026-10-07")
CBM_STD_RETURN(RUST_STD_STRING, "as_mut_str", "&str", false,
               "https://doc.rust-lang.org/std/string/struct.String.html#method.as_mut_str",
               "2026-10-07")
CBM_STD_RETURN(RUST_STD_STRING, "trim", "&str", false,
               "https://doc.rust-lang.org/std/string/struct.String.html#method.trim", "2026-10-07")
CBM_STD_RETURN(RUST_STD_STRING, "trim_start", "&str", false,
               "https://doc.rust-lang.org/std/string/struct.String.html#method.trim_start",
               "2026-10-07")
CBM_STD_RETURN(RUST_STD_STRING, "trim_end", "&str", false,
               "https://doc.rust-lang.org/std/string/struct.String.html#method.trim_end",
               "2026-10-07")
CBM_STD_RETURN(RUST_STD_STRING, "as_bytes", "&[u8]", false,
               "https://doc.rust-lang.org/std/string/struct.String.html#method.as_bytes",
               "2026-10-07")
CBM_STD_RETURN(RUST_STD_STRING, "into_bytes", "alloc.vec.Vec<u8>", false,
               "https://doc.rust-lang.org/std/string/struct.String.html#method.into_bytes",
               "2026-10-07")
CBM_STD_RETURN(RUST_STD_STRING, "len", "usize", false,
               "https://doc.rust-lang.org/std/string/struct.String.html#method.len", "2026-10-07")
CBM_STD_RETURN(RUST_STD_STRING, "capacity", "usize", false,
               "https://doc.rust-lang.org/std/string/struct.String.html#method.capacity",
               "2026-10-07")
CBM_STD_RETURN(RUST_STD_STRING, "is_empty", "bool", false,
               "https://doc.rust-lang.org/std/string/struct.String.html#method.is_empty",
               "2026-10-07")
CBM_STD_RETURN(RUST_STD_STRING, "starts_with", "bool", false,
               "https://doc.rust-lang.org/std/string/struct.String.html#method.starts_with",
               "2026-10-07")
CBM_STD_RETURN(RUST_STD_STRING, "ends_with", "bool", false,
               "https://doc.rust-lang.org/std/string/struct.String.html#method.ends_with",
               "2026-10-07")
CBM_STD_RETURN(RUST_STD_STRING, "contains", "bool", false,
               "https://doc.rust-lang.org/std/string/struct.String.html#method.contains",
               "2026-10-07")
CBM_STD_RETURN(RUST_STD_STRING, "pop", RS_OPT("char"), false,
               "https://doc.rust-lang.org/std/string/struct.String.html#method.pop", "2026-10-07")
CBM_STD_RETURN(RUST_STD_STRING, "remove", "char", false,
               "https://doc.rust-lang.org/std/string/struct.String.html#method.remove",
               "2026-10-07")
CBM_STD_RETURN(RUST_STD_STRING, "push", "()", false,
               "https://doc.rust-lang.org/std/string/struct.String.html#method.push", "2026-10-07")
CBM_STD_RETURN(RUST_STD_STRING, "push_str", "()", false,
               "https://doc.rust-lang.org/std/string/struct.String.html#method.push_str",
               "2026-10-07")
CBM_STD_RETURN(RUST_STD_STRING, "insert", "()", false,
               "https://doc.rust-lang.org/std/string/struct.String.html#method.insert",
               "2026-10-07")
CBM_STD_RETURN(RUST_STD_STRING, "insert_str", "()", false,
               "https://doc.rust-lang.org/std/string/struct.String.html#method.insert_str",
               "2026-10-07")
CBM_STD_RETURN(RUST_STD_STRING, "clear", "()", false,
               "https://doc.rust-lang.org/std/string/struct.String.html#method.clear", "2026-10-07")
CBM_STD_RETURN(RUST_STD_STRING, "truncate", "()", false,
               "https://doc.rust-lang.org/std/string/struct.String.html#method.truncate",
               "2026-10-07")
CBM_STD_RETURN(RUST_STD_VEC, "clone", "Self", false,
               "https://doc.rust-lang.org/std/vec/struct.Vec.html#method.clone", "2026-10-07")
CBM_STD_RETURN(RUST_STD_VEC, "to_vec", "Self", false,
               "https://doc.rust-lang.org/std/vec/struct.Vec.html#method.to_vec", "2026-10-07")
CBM_STD_RETURN(RUST_STD_VEC, "split_off", "Self", false,
               "https://doc.rust-lang.org/std/vec/struct.Vec.html#method.split_off", "2026-10-07")
CBM_STD_RETURN(RUST_STD_VEC, "len", "usize", false,
               "https://doc.rust-lang.org/std/vec/struct.Vec.html#method.len", "2026-10-07")
CBM_STD_RETURN(RUST_STD_VEC, "capacity", "usize", false,
               "https://doc.rust-lang.org/std/vec/struct.Vec.html#method.capacity", "2026-10-07")
CBM_STD_RETURN(RUST_STD_VEC, "is_empty", "bool", false,
               "https://doc.rust-lang.org/std/vec/struct.Vec.html#method.is_empty", "2026-10-07")
CBM_STD_RETURN(RUST_STD_VEC, "contains", "bool", false,
               "https://doc.rust-lang.org/std/vec/struct.Vec.html#method.contains", "2026-10-07")
CBM_STD_RETURN(RUST_STD_VEC, "pop", RS_OPT("T"), false,
               "https://doc.rust-lang.org/std/vec/struct.Vec.html#method.pop", "2026-10-07")
CBM_STD_RETURN(RUST_STD_VEC, "remove", "T", false,
               "https://doc.rust-lang.org/std/vec/struct.Vec.html#method.remove", "2026-10-07")
CBM_STD_RETURN(RUST_STD_VEC, "swap_remove", "T", false,
               "https://doc.rust-lang.org/std/vec/struct.Vec.html#method.swap_remove", "2026-10-07")
CBM_STD_RETURN(RUST_STD_VEC, "first", RS_OPT("&T"), false,
               "https://doc.rust-lang.org/std/vec/struct.Vec.html#method.first", "2026-10-07")
CBM_STD_RETURN(RUST_STD_VEC, "last", RS_OPT("&T"), false,
               "https://doc.rust-lang.org/std/vec/struct.Vec.html#method.last", "2026-10-07")
CBM_STD_RETURN(RUST_STD_VEC, "first_mut", RS_OPT("!T"), false,
               "https://doc.rust-lang.org/std/vec/struct.Vec.html#method.first_mut", "2026-10-07")
CBM_STD_RETURN(RUST_STD_VEC, "last_mut", RS_OPT("!T"), false,
               "https://doc.rust-lang.org/std/vec/struct.Vec.html#method.last_mut", "2026-10-07")
CBM_STD_RETURN(RUST_STD_VEC, "as_slice", "&[T]", false,
               "https://doc.rust-lang.org/std/vec/struct.Vec.html#method.as_slice", "2026-10-07")
CBM_STD_RETURN(RUST_STD_VEC, "as_mut_slice", "&[T]", false,
               "https://doc.rust-lang.org/std/vec/struct.Vec.html#method.as_mut_slice",
               "2026-10-07")
CBM_STD_RETURN(RUST_STD_VEC, "iter", RS_ITER("&T"), false,
               "https://doc.rust-lang.org/std/vec/struct.Vec.html#method.iter", "2026-10-07")
CBM_STD_RETURN(RUST_STD_VEC, "iter_mut", RS_ITER("!T"), false,
               "https://doc.rust-lang.org/std/vec/struct.Vec.html#method.iter_mut", "2026-10-07")
CBM_STD_RETURN(RUST_STD_VEC, "into_iter", "core.iter.Iterator<T>", false,
               "https://doc.rust-lang.org/std/vec/struct.Vec.html#method.into_iter", "2026-10-07")
CBM_STD_RETURN(RUST_STD_VEC, "drain", "core.iter.Iterator<T>", false,
               "https://doc.rust-lang.org/std/vec/struct.Vec.html#method.drain", "2026-10-07")
CBM_STD_RETURN(RUST_STD_VEC, "windows", RS_ITER("&[T]"), false,
               "https://doc.rust-lang.org/std/vec/struct.Vec.html#method.windows", "2026-10-07")
CBM_STD_RETURN(RUST_STD_VEC, "chunks", RS_ITER("&[T]"), false,
               "https://doc.rust-lang.org/std/vec/struct.Vec.html#method.chunks", "2026-10-07")
CBM_STD_RETURN(RUST_STD_VEC, "chunks_mut", RS_ITER("![T]"), false,
               "https://doc.rust-lang.org/std/vec/struct.Vec.html#method.chunks_mut", "2026-10-07")
CBM_STD_RETURN(RUST_STD_VEC, "push", "()", false,
               "https://doc.rust-lang.org/std/vec/struct.Vec.html#method.push", "2026-10-07")
CBM_STD_RETURN(RUST_STD_VEC, "insert", "()", false,
               "https://doc.rust-lang.org/std/vec/struct.Vec.html#method.insert", "2026-10-07")
CBM_STD_RETURN(RUST_STD_VEC, "clear", "()", false,
               "https://doc.rust-lang.org/std/vec/struct.Vec.html#method.clear", "2026-10-07")
CBM_STD_RETURN(RUST_STD_VEC, "truncate", "()", false,
               "https://doc.rust-lang.org/std/vec/struct.Vec.html#method.truncate", "2026-10-07")
CBM_STD_RETURN(RUST_STD_VEC, "extend", "()", false,
               "https://doc.rust-lang.org/std/vec/struct.Vec.html#method.extend", "2026-10-07")
CBM_STD_RETURN(RUST_STD_VEC, "extend_from_slice", "()", false,
               "https://doc.rust-lang.org/std/vec/struct.Vec.html#method.extend_from_slice",
               "2026-10-07")
CBM_STD_RETURN(RUST_STD_VEC, "retain", "()", false,
               "https://doc.rust-lang.org/std/vec/struct.Vec.html#method.retain", "2026-10-07")
CBM_STD_RETURN(RUST_STD_VEC, "dedup", "()", false,
               "https://doc.rust-lang.org/std/vec/struct.Vec.html#method.dedup", "2026-10-07")
CBM_STD_RETURN(RUST_STD_VEC, "reverse", "()", false,
               "https://doc.rust-lang.org/std/vec/struct.Vec.html#method.reverse", "2026-10-07")
CBM_STD_RETURN(RUST_STD_VEC, "sort", "()", false,
               "https://doc.rust-lang.org/std/vec/struct.Vec.html#method.sort", "2026-10-07")
CBM_STD_RETURN(RUST_STD_VEC, "sort_by", "()", false,
               "https://doc.rust-lang.org/std/vec/struct.Vec.html#method.sort_by", "2026-10-07")
CBM_STD_RETURN(RUST_STD_VEC, "sort_by_key", "()", false,
               "https://doc.rust-lang.org/std/vec/struct.Vec.html#method.sort_by_key", "2026-10-07")
CBM_STD_RETURN(RUST_STD_VEC, "sort_unstable", "()", false,
               "https://doc.rust-lang.org/std/vec/struct.Vec.html#method.sort_unstable",
               "2026-10-07")
CBM_STD_RETURN(RUST_STD_ANY, "downcast_ref", RS_OPT("&G"), false,
               "https://doc.rust-lang.org/std/any/trait.Any.html#method.downcast_ref", "2026-10-07")
CBM_STD_RETURN(RUST_STD_ANY, "downcast_mut", RS_OPT("!G"), false,
               "https://doc.rust-lang.org/std/any/trait.Any.html#method.downcast_mut", "2026-10-07")
CBM_STD_RETURN(RUST_STD_ANY, "is", "bool", false,
               "https://doc.rust-lang.org/std/any/trait.Any.html#method.is", "2026-10-07")
CBM_STD_RETURN(RUST_STD_ERROR, "downcast_ref", RS_OPT("&G"), false,
               "https://doc.rust-lang.org/std/error/trait.Error.html#method.downcast_ref",
               "2026-10-07")
CBM_STD_RETURN(RUST_STD_ERROR, "downcast_mut", RS_OPT("!G"), false,
               "https://doc.rust-lang.org/std/error/trait.Error.html#method.downcast_mut",
               "2026-10-07")
CBM_STD_RETURN(RUST_STD_ERROR, "is", "bool", false,
               "https://doc.rust-lang.org/std/error/trait.Error.html#method.is", "2026-10-07")
CBM_STD_RETURN(RUST_STD_ONCELOCK, "get", RS_OPT("&T"), false,
               "https://doc.rust-lang.org/std/sync/struct.OnceLock.html#method.get", "2026-10-07")
CBM_STD_RETURN(RUST_STD_ONCELOCK, "get_mut", RS_OPT("!T"), false,
               "https://doc.rust-lang.org/std/sync/struct.OnceLock.html#method.get_mut",
               "2026-10-07")
CBM_STD_RETURN(RUST_STD_ONCELOCK, "get_or_init", "&T", false,
               "https://doc.rust-lang.org/std/sync/struct.OnceLock.html#method.get_or_init",
               "2026-10-07")
CBM_STD_RETURN(RUST_STD_ONCELOCK, "set", RS_RES("()", "T"), false,
               "https://doc.rust-lang.org/std/sync/struct.OnceLock.html#method.set", "2026-10-07")
CBM_STD_RETURN(RUST_STD_ONCELOCK, "into_inner", RS_OPT("T"), false,
               "https://doc.rust-lang.org/std/sync/struct.OnceLock.html#method.into_inner",
               "2026-10-07")
CBM_STD_RETURN(RUST_STD_ONCELOCK, "take", RS_OPT("T"), false,
               "https://doc.rust-lang.org/std/sync/struct.OnceLock.html#method.take", "2026-10-07")
#endif

#ifdef CBM_STD_CLOSURE
CBM_STD_CLOSURE(RUST_CLOSURE_RECV_OPTION, "map", 0, "T",
                "https://doc.rust-lang.org/std/option/enum.Option.html#method.map", "2026-10-07")
CBM_STD_CLOSURE(RUST_CLOSURE_RECV_OPTION, "and_then", 0, "T",
                "https://doc.rust-lang.org/std/option/enum.Option.html#method.and_then",
                "2026-10-07")
CBM_STD_CLOSURE(RUST_CLOSURE_RECV_OPTION, "is_some_and", 0, "T",
                "https://doc.rust-lang.org/std/option/enum.Option.html#method.is_some_and",
                "2026-10-07")
CBM_STD_CLOSURE(RUST_CLOSURE_RECV_OPTION, "is_none_or", 0, "T",
                "https://doc.rust-lang.org/std/option/enum.Option.html#method.is_none_or",
                "2026-10-07")
CBM_STD_CLOSURE(RUST_CLOSURE_RECV_OPTION, "filter", 0, "&T",
                "https://doc.rust-lang.org/std/option/enum.Option.html#method.filter", "2026-10-07")
CBM_STD_CLOSURE(RUST_CLOSURE_RECV_OPTION, "inspect", 0, "&T",
                "https://doc.rust-lang.org/std/option/enum.Option.html#method.inspect",
                "2026-10-07")
CBM_STD_CLOSURE(RUST_CLOSURE_RECV_OPTION, "take_if", 0, "&T",
                "https://doc.rust-lang.org/std/option/enum.Option.html#method.take_if",
                "2026-10-07")
CBM_STD_CLOSURE(RUST_CLOSURE_RECV_OPTION, "map_or", 1, "T",
                "https://doc.rust-lang.org/std/option/enum.Option.html#method.map_or", "2026-10-07")
CBM_STD_CLOSURE(RUST_CLOSURE_RECV_OPTION, "map_or_else", 1, "T",
                "https://doc.rust-lang.org/std/option/enum.Option.html#method.map_or_else",
                "2026-10-07")
CBM_STD_CLOSURE(RUST_CLOSURE_RECV_RESULT, "map", 0, "T",
                "https://doc.rust-lang.org/std/result/enum.Result.html#method.map", "2026-10-07")
CBM_STD_CLOSURE(RUST_CLOSURE_RECV_RESULT, "and_then", 0, "T",
                "https://doc.rust-lang.org/std/result/enum.Result.html#method.and_then",
                "2026-10-07")
CBM_STD_CLOSURE(RUST_CLOSURE_RECV_RESULT, "is_ok_and", 0, "T",
                "https://doc.rust-lang.org/std/result/enum.Result.html#method.is_ok_and",
                "2026-10-07")
CBM_STD_CLOSURE(RUST_CLOSURE_RECV_RESULT, "inspect", 0, "&T",
                "https://doc.rust-lang.org/std/result/enum.Result.html#method.inspect",
                "2026-10-07")
CBM_STD_CLOSURE(RUST_CLOSURE_RECV_RESULT, "map_or", 1, "T",
                "https://doc.rust-lang.org/std/result/enum.Result.html#method.map_or", "2026-10-07")
CBM_STD_CLOSURE(RUST_CLOSURE_RECV_RESULT, "map_or_else", 1, "T",
                "https://doc.rust-lang.org/std/result/enum.Result.html#method.map_or_else",
                "2026-10-07")
CBM_STD_CLOSURE(RUST_CLOSURE_RECV_RESULT, "map_or_else", 0, "E",
                "https://doc.rust-lang.org/std/result/enum.Result.html#method.map_or_else",
                "2026-10-07")
CBM_STD_CLOSURE(RUST_CLOSURE_RECV_RESULT, "map_err", 0, "E",
                "https://doc.rust-lang.org/std/result/enum.Result.html#method.map_err",
                "2026-10-07")
CBM_STD_CLOSURE(RUST_CLOSURE_RECV_RESULT, "or_else", 0, "E",
                "https://doc.rust-lang.org/std/result/enum.Result.html#method.or_else",
                "2026-10-07")
CBM_STD_CLOSURE(RUST_CLOSURE_RECV_RESULT, "unwrap_or_else", 0, "E",
                "https://doc.rust-lang.org/std/result/enum.Result.html#method.unwrap_or_else",
                "2026-10-07")
CBM_STD_CLOSURE(RUST_CLOSURE_RECV_RESULT, "is_err_and", 0, "E",
                "https://doc.rust-lang.org/std/result/enum.Result.html#method.is_err_and",
                "2026-10-07")
CBM_STD_CLOSURE(RUST_CLOSURE_RECV_RESULT, "inspect_err", 0, "&E",
                "https://doc.rust-lang.org/std/result/enum.Result.html#method.inspect_err",
                "2026-10-07")
CBM_STD_CLOSURE(RUST_CLOSURE_RECV_ITER, "map", 0, "T",
                "https://doc.rust-lang.org/std/iter/trait.Iterator.html#method.map", "2026-10-07")
CBM_STD_CLOSURE(RUST_CLOSURE_RECV_ITER, "for_each", 0, "T",
                "https://doc.rust-lang.org/std/iter/trait.Iterator.html#method.for_each",
                "2026-10-07")
CBM_STD_CLOSURE(RUST_CLOSURE_RECV_ITER, "try_for_each", 0, "T",
                "https://doc.rust-lang.org/std/iter/trait.Iterator.html#method.try_for_each",
                "2026-10-07")
CBM_STD_CLOSURE(RUST_CLOSURE_RECV_ITER, "filter_map", 0, "T",
                "https://doc.rust-lang.org/std/iter/trait.Iterator.html#method.filter_map",
                "2026-10-07")
CBM_STD_CLOSURE(RUST_CLOSURE_RECV_ITER, "flat_map", 0, "T",
                "https://doc.rust-lang.org/std/iter/trait.Iterator.html#method.flat_map",
                "2026-10-07")
CBM_STD_CLOSURE(RUST_CLOSURE_RECV_ITER, "find_map", 0, "T",
                "https://doc.rust-lang.org/std/iter/trait.Iterator.html#method.find_map",
                "2026-10-07")
CBM_STD_CLOSURE(RUST_CLOSURE_RECV_ITER, "map_while", 0, "T",
                "https://doc.rust-lang.org/std/iter/trait.Iterator.html#method.map_while",
                "2026-10-07")
CBM_STD_CLOSURE(RUST_CLOSURE_RECV_ITER, "any", 0, "T",
                "https://doc.rust-lang.org/std/iter/trait.Iterator.html#method.any", "2026-10-07")
CBM_STD_CLOSURE(RUST_CLOSURE_RECV_ITER, "all", 0, "T",
                "https://doc.rust-lang.org/std/iter/trait.Iterator.html#method.all", "2026-10-07")
CBM_STD_CLOSURE(RUST_CLOSURE_RECV_ITER, "position", 0, "T",
                "https://doc.rust-lang.org/std/iter/trait.Iterator.html#method.position",
                "2026-10-07")
CBM_STD_CLOSURE(RUST_CLOSURE_RECV_ITER, "rposition", 0, "T",
                "https://doc.rust-lang.org/std/iter/trait.Iterator.html#method.rposition",
                "2026-10-07")
CBM_STD_CLOSURE(RUST_CLOSURE_RECV_ITER, "filter", 0, "&T",
                "https://doc.rust-lang.org/std/iter/trait.Iterator.html#method.filter",
                "2026-10-07")
CBM_STD_CLOSURE(RUST_CLOSURE_RECV_ITER, "find", 0, "&T",
                "https://doc.rust-lang.org/std/iter/trait.Iterator.html#method.find", "2026-10-07")
CBM_STD_CLOSURE(RUST_CLOSURE_RECV_ITER, "rfind", 0, "&T",
                "https://doc.rust-lang.org/std/iter/trait.Iterator.html#method.rfind", "2026-10-07")
CBM_STD_CLOSURE(RUST_CLOSURE_RECV_ITER, "take_while", 0, "&T",
                "https://doc.rust-lang.org/std/iter/trait.Iterator.html#method.take_while",
                "2026-10-07")
CBM_STD_CLOSURE(RUST_CLOSURE_RECV_ITER, "skip_while", 0, "&T",
                "https://doc.rust-lang.org/std/iter/trait.Iterator.html#method.skip_while",
                "2026-10-07")
CBM_STD_CLOSURE(RUST_CLOSURE_RECV_ITER, "inspect", 0, "&T",
                "https://doc.rust-lang.org/std/iter/trait.Iterator.html#method.inspect",
                "2026-10-07")
CBM_STD_CLOSURE(RUST_CLOSURE_RECV_ITER, "partition", 0, "&T",
                "https://doc.rust-lang.org/std/iter/trait.Iterator.html#method.partition",
                "2026-10-07")
CBM_STD_CLOSURE(RUST_CLOSURE_RECV_ITER, "max_by_key", 0, "&T",
                "https://doc.rust-lang.org/std/iter/trait.Iterator.html#method.max_by_key",
                "2026-10-07")
CBM_STD_CLOSURE(RUST_CLOSURE_RECV_ITER, "min_by_key", 0, "&T",
                "https://doc.rust-lang.org/std/iter/trait.Iterator.html#method.min_by_key",
                "2026-10-07")
CBM_STD_CLOSURE(RUST_CLOSURE_RECV_ITER, "max_by", 0, "&T,&T",
                "https://doc.rust-lang.org/std/iter/trait.Iterator.html#method.max_by",
                "2026-10-07")
CBM_STD_CLOSURE(RUST_CLOSURE_RECV_ITER, "min_by", 0, "&T,&T",
                "https://doc.rust-lang.org/std/iter/trait.Iterator.html#method.min_by",
                "2026-10-07")
CBM_STD_CLOSURE(RUST_CLOSURE_RECV_ITER, "reduce", 0, "T,T",
                "https://doc.rust-lang.org/std/iter/trait.Iterator.html#method.reduce",
                "2026-10-07")
CBM_STD_CLOSURE(RUST_CLOSURE_RECV_ITER, "fold", 1, "A,T",
                "https://doc.rust-lang.org/std/iter/trait.Iterator.html#method.fold", "2026-10-07")
CBM_STD_CLOSURE(RUST_CLOSURE_RECV_SEQ, "retain", 0, "&T",
                "https://doc.rust-lang.org/std/primitive.slice.html#method.retain", "2026-10-07")
CBM_STD_CLOSURE(RUST_CLOSURE_RECV_SEQ, "retain_mut", 0, "&T",
                "https://doc.rust-lang.org/std/primitive.slice.html#method.retain_mut",
                "2026-10-07")
CBM_STD_CLOSURE(RUST_CLOSURE_RECV_SEQ, "dedup_by_key", 0, "&T",
                "https://doc.rust-lang.org/std/primitive.slice.html#method.dedup_by_key",
                "2026-10-07")
CBM_STD_CLOSURE(RUST_CLOSURE_RECV_SEQ, "dedup_by", 0, "&T,&T",
                "https://doc.rust-lang.org/std/primitive.slice.html#method.dedup_by", "2026-10-07")
CBM_STD_CLOSURE(RUST_CLOSURE_RECV_SEQ, "sort_by", 0, "&T,&T",
                "https://doc.rust-lang.org/std/primitive.slice.html#method.sort_by", "2026-10-07")
CBM_STD_CLOSURE(RUST_CLOSURE_RECV_SEQ, "sort_unstable_by", 0, "&T,&T",
                "https://doc.rust-lang.org/std/primitive.slice.html#method.sort_unstable_by",
                "2026-10-07")
CBM_STD_CLOSURE(RUST_CLOSURE_RECV_SEQ, "sort_by_key", 0, "&T",
                "https://doc.rust-lang.org/std/primitive.slice.html#method.sort_by_key",
                "2026-10-07")
CBM_STD_CLOSURE(RUST_CLOSURE_RECV_SEQ, "sort_unstable_by_key", 0, "&T",
                "https://doc.rust-lang.org/std/primitive.slice.html#method.sort_unstable_by_key",
                "2026-10-07")
CBM_STD_CLOSURE(RUST_CLOSURE_RECV_SEQ, "sort_by_cached_key", 0, "&T",
                "https://doc.rust-lang.org/std/primitive.slice.html#method.sort_by_cached_key",
                "2026-10-07")
CBM_STD_CLOSURE(RUST_CLOSURE_RECV_SEQ, "binary_search_by", 0, "&T",
                "https://doc.rust-lang.org/std/primitive.slice.html#method.binary_search_by",
                "2026-10-07")
CBM_STD_CLOSURE(RUST_CLOSURE_RECV_SEQ, "binary_search_by_key", 1, "&T",
                "https://doc.rust-lang.org/std/primitive.slice.html#method.binary_search_by_key",
                "2026-10-07")
CBM_STD_CLOSURE(RUST_CLOSURE_RECV_SEQ, "partition_point", 0, "&T",
                "https://doc.rust-lang.org/std/primitive.slice.html#method.partition_point",
                "2026-10-07")
CBM_STD_CLOSURE(RUST_CLOSURE_RECV_SEQ, "chunk_by", 0, "&T,&T",
                "https://doc.rust-lang.org/std/primitive.slice.html#method.chunk_by", "2026-10-07")
CBM_STD_CLOSURE(RUST_CLOSURE_RECV_SEQ, "split", 0, "&T",
                "https://doc.rust-lang.org/std/primitive.slice.html#method.split", "2026-10-07")
CBM_STD_CLOSURE(RUST_CLOSURE_RECV_SET, "retain", 0, "&T",
                "https://doc.rust-lang.org/std/collections/struct.HashSet.html#method.retain",
                "2026-10-07")
CBM_STD_CLOSURE(RUST_CLOSURE_RECV_MAP, "retain", 0, "&T,&E",
                "https://doc.rust-lang.org/std/collections/struct.HashMap.html#method.retain",
                "2026-10-07")
#endif

#ifdef CBM_STD_PRODUCER
CBM_STD_PRODUCER("boxed.Box", "pin", "core.pin.Pin<alloc.boxed.Box<P>>",
                 "https://doc.rust-lang.org/std/boxed/struct.Box.html#method.pin", "2026-10-07")
CBM_STD_PRODUCER("sync.Arc", "new_cyclic", "alloc.sync.Arc<F>",
                 "https://doc.rust-lang.org/std/sync/struct.Arc.html#method.new_cyclic",
                 "2026-10-07")
CBM_STD_PRODUCER("rc.Rc", "new_cyclic", "alloc.rc.Rc<F>",
                 "https://doc.rust-lang.org/std/rc/struct.Rc.html#method.new_cyclic", "2026-10-07")
CBM_STD_PRODUCER("panic", "catch_unwind", RS_RES("F", "_"),
                 "https://doc.rust-lang.org/std/panic/fn.catch_unwind.html", "2026-10-07")
CBM_STD_PRODUCER("iter", "from_fn", RS_ITER("?F"),
                 "https://doc.rust-lang.org/std/iter/fn.from_fn.html", "2026-10-07")
CBM_STD_PRODUCER("iter", "once", RS_ITER("A"), "https://doc.rust-lang.org/std/iter/fn.once.html",
                 "2026-10-07")
CBM_STD_PRODUCER("pin.Pin", "new", "core.pin.Pin<A>",
                 "https://doc.rust-lang.org/std/pin/struct.Pin.html#method.new", "2026-10-07")
CBM_STD_PRODUCER("cell.Cell", "new", "core.cell.Cell<A>",
                 "https://doc.rust-lang.org/std/cell/struct.Cell.html#method.new", "2026-10-07")
CBM_STD_PRODUCER("fs", "canonicalize", RS_RES(RS_PATHBUF, "_"),
                 "https://doc.rust-lang.org/std/fs/fn.canonicalize.html", "2026-10-07")
CBM_STD_PRODUCER("collections.VecDeque", "from", "std.collections.VecDeque<?A>",
                 "https://doc.rust-lang.org/std/collections/struct.VecDeque.html#method.from",
                 "2026-10-07")
CBM_STD_PRODUCER("mem.ManuallyDrop", "into_inner", "?A",
                 "https://doc.rust-lang.org/std/mem/struct.ManuallyDrop.html#method.into_inner",
                 "2026-10-07")
#endif

#ifdef CBM_STD_LEGACY
CBM_STD_LEGACY("Vec", "iter", "seq_iterator_first",
               "https://doc.rust-lang.org/std/vec/struct.Vec.html#method.iter", "2026-10-07")
CBM_STD_LEGACY("Vec", "iter_mut", "seq_iterator_first",
               "https://doc.rust-lang.org/std/vec/struct.Vec.html#method.iter_mut", "2026-10-07")
CBM_STD_LEGACY("Vec", "into_iter", "seq_iterator_first",
               "https://doc.rust-lang.org/std/vec/struct.Vec.html#method.into_iter", "2026-10-07")
CBM_STD_LEGACY("Vec", "drain", "seq_iterator_first",
               "https://doc.rust-lang.org/std/vec/struct.Vec.html#method.drain", "2026-10-07")
CBM_STD_LEGACY("VecDeque", "iter", "seq_iterator_first",
               "https://doc.rust-lang.org/std/collections/struct.VecDeque.html#method.iter",
               "2026-10-07")
CBM_STD_LEGACY("VecDeque", "iter_mut", "seq_iterator_first",
               "https://doc.rust-lang.org/std/collections/struct.VecDeque.html#method.iter_mut",
               "2026-10-07")
CBM_STD_LEGACY("VecDeque", "into_iter", "seq_iterator_first",
               "https://doc.rust-lang.org/std/collections/struct.VecDeque.html#method.into_iter",
               "2026-10-07")
CBM_STD_LEGACY("VecDeque", "drain", "seq_iterator_first",
               "https://doc.rust-lang.org/std/collections/struct.VecDeque.html#method.drain",
               "2026-10-07")
CBM_STD_LEGACY("HashSet", "iter", "seq_iterator_first",
               "https://doc.rust-lang.org/std/collections/struct.HashSet.html#method.iter",
               "2026-10-07")
CBM_STD_LEGACY("HashSet", "iter_mut", "seq_iterator_first",
               "https://doc.rust-lang.org/std/collections/struct.HashSet.html#method.iter_mut",
               "2026-10-07")
CBM_STD_LEGACY("HashSet", "into_iter", "seq_iterator_first",
               "https://doc.rust-lang.org/std/collections/struct.HashSet.html#method.into_iter",
               "2026-10-07")
CBM_STD_LEGACY("HashSet", "drain", "seq_iterator_first",
               "https://doc.rust-lang.org/std/collections/struct.HashSet.html#method.drain",
               "2026-10-07")
CBM_STD_LEGACY("BTreeSet", "iter", "seq_iterator_first",
               "https://doc.rust-lang.org/std/collections/struct.BTreeSet.html#method.iter",
               "2026-10-07")
CBM_STD_LEGACY("BTreeSet", "iter_mut", "seq_iterator_first",
               "https://doc.rust-lang.org/std/collections/struct.BTreeSet.html#method.iter_mut",
               "2026-10-07")
CBM_STD_LEGACY("BTreeSet", "into_iter", "seq_iterator_first",
               "https://doc.rust-lang.org/std/collections/struct.BTreeSet.html#method.into_iter",
               "2026-10-07")
CBM_STD_LEGACY("BTreeSet", "drain", "seq_iterator_first",
               "https://doc.rust-lang.org/std/collections/struct.BTreeSet.html#method.drain",
               "2026-10-07")
CBM_STD_LEGACY("Vec", "remove", "seq_first_generic",
               "https://doc.rust-lang.org/std/vec/struct.Vec.html#method.remove", "2026-10-07")
CBM_STD_LEGACY("Vec", "swap_remove", "seq_first_generic",
               "https://doc.rust-lang.org/std/vec/struct.Vec.html#method.swap_remove", "2026-10-07")
CBM_STD_LEGACY("VecDeque", "remove", "seq_first_generic",
               "https://doc.rust-lang.org/std/collections/struct.VecDeque.html#method.remove",
               "2026-10-07")
CBM_STD_LEGACY("VecDeque", "swap_remove", "seq_first_generic",
               "https://doc.rust-lang.org/std/collections/struct.VecDeque.html#method.swap_remove",
               "2026-10-07")
CBM_STD_LEGACY("HashSet", "remove", "seq_first_generic",
               "https://doc.rust-lang.org/std/collections/struct.HashSet.html#method.remove",
               "2026-10-07")
CBM_STD_LEGACY("HashSet", "swap_remove", "seq_first_generic",
               "https://doc.rust-lang.org/std/collections/struct.HashSet.html#method.swap_remove",
               "2026-10-07")
CBM_STD_LEGACY("BTreeSet", "remove", "seq_first_generic",
               "https://doc.rust-lang.org/std/collections/struct.BTreeSet.html#method.remove",
               "2026-10-07")
CBM_STD_LEGACY("BTreeSet", "swap_remove", "seq_first_generic",
               "https://doc.rust-lang.org/std/collections/struct.BTreeSet.html#method.swap_remove",
               "2026-10-07")
CBM_STD_LEGACY("Vec", "len", "seq_usize",
               "https://doc.rust-lang.org/std/vec/struct.Vec.html#method.len", "2026-10-07")
CBM_STD_LEGACY("Vec", "capacity", "seq_usize",
               "https://doc.rust-lang.org/std/vec/struct.Vec.html#method.capacity", "2026-10-07")
CBM_STD_LEGACY("VecDeque", "len", "seq_usize",
               "https://doc.rust-lang.org/std/collections/struct.VecDeque.html#method.len",
               "2026-10-07")
CBM_STD_LEGACY("VecDeque", "capacity", "seq_usize",
               "https://doc.rust-lang.org/std/collections/struct.VecDeque.html#method.capacity",
               "2026-10-07")
CBM_STD_LEGACY("HashSet", "len", "seq_usize",
               "https://doc.rust-lang.org/std/collections/struct.HashSet.html#method.len",
               "2026-10-07")
CBM_STD_LEGACY("HashSet", "capacity", "seq_usize",
               "https://doc.rust-lang.org/std/collections/struct.HashSet.html#method.capacity",
               "2026-10-07")
CBM_STD_LEGACY("BTreeSet", "len", "seq_usize",
               "https://doc.rust-lang.org/std/collections/struct.BTreeSet.html#method.len",
               "2026-10-07")
CBM_STD_LEGACY("BTreeSet", "capacity", "seq_usize",
               "https://doc.rust-lang.org/std/collections/struct.BTreeSet.html#method.capacity",
               "2026-10-07")
CBM_STD_LEGACY("Vec", "is_empty", "seq_bool",
               "https://doc.rust-lang.org/std/vec/struct.Vec.html#method.is_empty", "2026-10-07")
CBM_STD_LEGACY("Vec", "contains", "seq_bool",
               "https://doc.rust-lang.org/std/vec/struct.Vec.html#method.contains", "2026-10-07")
CBM_STD_LEGACY("VecDeque", "is_empty", "seq_bool",
               "https://doc.rust-lang.org/std/collections/struct.VecDeque.html#method.is_empty",
               "2026-10-07")
CBM_STD_LEGACY("VecDeque", "contains", "seq_bool",
               "https://doc.rust-lang.org/std/collections/struct.VecDeque.html#method.contains",
               "2026-10-07")
CBM_STD_LEGACY("HashSet", "is_empty", "seq_bool",
               "https://doc.rust-lang.org/std/collections/struct.HashSet.html#method.is_empty",
               "2026-10-07")
CBM_STD_LEGACY("HashSet", "contains", "seq_bool",
               "https://doc.rust-lang.org/std/collections/struct.HashSet.html#method.contains",
               "2026-10-07")
CBM_STD_LEGACY("BTreeSet", "is_empty", "seq_bool",
               "https://doc.rust-lang.org/std/collections/struct.BTreeSet.html#method.is_empty",
               "2026-10-07")
CBM_STD_LEGACY("BTreeSet", "contains", "seq_bool",
               "https://doc.rust-lang.org/std/collections/struct.BTreeSet.html#method.contains",
               "2026-10-07")
CBM_STD_LEGACY("Vec", "first", "seq_option_first",
               "https://doc.rust-lang.org/std/vec/struct.Vec.html#method.first", "2026-10-07")
CBM_STD_LEGACY("Vec", "last", "seq_option_first",
               "https://doc.rust-lang.org/std/vec/struct.Vec.html#method.last", "2026-10-07")
CBM_STD_LEGACY("Vec", "get", "seq_option_first",
               "https://doc.rust-lang.org/std/vec/struct.Vec.html#method.get", "2026-10-07")
CBM_STD_LEGACY("Vec", "pop", "seq_option_first",
               "https://doc.rust-lang.org/std/vec/struct.Vec.html#method.pop", "2026-10-07")
CBM_STD_LEGACY("VecDeque", "first", "seq_option_first",
               "https://doc.rust-lang.org/std/collections/struct.VecDeque.html#method.first",
               "2026-10-07")
CBM_STD_LEGACY("VecDeque", "last", "seq_option_first",
               "https://doc.rust-lang.org/std/collections/struct.VecDeque.html#method.last",
               "2026-10-07")
CBM_STD_LEGACY("VecDeque", "get", "seq_option_first",
               "https://doc.rust-lang.org/std/collections/struct.VecDeque.html#method.get",
               "2026-10-07")
CBM_STD_LEGACY("VecDeque", "pop", "seq_option_first",
               "https://doc.rust-lang.org/std/collections/struct.VecDeque.html#method.pop",
               "2026-10-07")
CBM_STD_LEGACY("HashSet", "first", "seq_option_first",
               "https://doc.rust-lang.org/std/collections/struct.HashSet.html#method.first",
               "2026-10-07")
CBM_STD_LEGACY("HashSet", "last", "seq_option_first",
               "https://doc.rust-lang.org/std/collections/struct.HashSet.html#method.last",
               "2026-10-07")
CBM_STD_LEGACY("HashSet", "get", "seq_option_first",
               "https://doc.rust-lang.org/std/collections/struct.HashSet.html#method.get",
               "2026-10-07")
CBM_STD_LEGACY("HashSet", "pop", "seq_option_first",
               "https://doc.rust-lang.org/std/collections/struct.HashSet.html#method.pop",
               "2026-10-07")
CBM_STD_LEGACY("BTreeSet", "first", "seq_option_first",
               "https://doc.rust-lang.org/std/collections/struct.BTreeSet.html#method.first",
               "2026-10-07")
CBM_STD_LEGACY("BTreeSet", "last", "seq_option_first",
               "https://doc.rust-lang.org/std/collections/struct.BTreeSet.html#method.last",
               "2026-10-07")
CBM_STD_LEGACY("BTreeSet", "get", "seq_option_first",
               "https://doc.rust-lang.org/std/collections/struct.BTreeSet.html#method.get",
               "2026-10-07")
CBM_STD_LEGACY("BTreeSet", "pop", "seq_option_first",
               "https://doc.rust-lang.org/std/collections/struct.BTreeSet.html#method.pop",
               "2026-10-07")
CBM_STD_LEGACY("Vec", "as_slice", "seq_slice_first",
               "https://doc.rust-lang.org/std/vec/struct.Vec.html#method.as_slice", "2026-10-07")
CBM_STD_LEGACY("VecDeque", "as_slice", "seq_slice_first",
               "https://doc.rust-lang.org/std/collections/struct.VecDeque.html#method.as_slice",
               "2026-10-07")
CBM_STD_LEGACY("HashSet", "as_slice", "seq_slice_first",
               "https://doc.rust-lang.org/std/collections/struct.HashSet.html#method.as_slice",
               "2026-10-07")
CBM_STD_LEGACY("BTreeSet", "as_slice", "seq_slice_first",
               "https://doc.rust-lang.org/std/collections/struct.BTreeSet.html#method.as_slice",
               "2026-10-07")
CBM_STD_LEGACY("Iterator", "collect", "iter_collect",
               "https://doc.rust-lang.org/std/iter/trait.Iterator.html#method.collect",
               "2026-10-07")
CBM_STD_LEGACY("Iterator", "count", "iter_usize",
               "https://doc.rust-lang.org/std/iter/trait.Iterator.html#method.count", "2026-10-07")
CBM_STD_LEGACY("Iterator", "len", "iter_usize",
               "https://doc.rust-lang.org/std/iter/trait.Iterator.html#method.len", "2026-10-07")
CBM_STD_LEGACY("Iterator", "next", "iter_option_first",
               "https://doc.rust-lang.org/std/iter/trait.Iterator.html#method.next", "2026-10-07")
CBM_STD_LEGACY("Iterator", "last", "iter_option_first",
               "https://doc.rust-lang.org/std/iter/trait.Iterator.html#method.last", "2026-10-07")
CBM_STD_LEGACY("Iterator", "nth", "iter_option_first",
               "https://doc.rust-lang.org/std/iter/trait.Iterator.html#method.nth", "2026-10-07")
CBM_STD_LEGACY("Iterator", "find", "iter_option_first",
               "https://doc.rust-lang.org/std/iter/trait.Iterator.html#method.find", "2026-10-07")
CBM_STD_LEGACY("Iterator", "max", "iter_option_first",
               "https://doc.rust-lang.org/std/iter/trait.Iterator.html#method.max", "2026-10-07")
CBM_STD_LEGACY("Iterator", "min", "iter_option_first",
               "https://doc.rust-lang.org/std/iter/trait.Iterator.html#method.min", "2026-10-07")
CBM_STD_LEGACY("Iterator", "max_by", "iter_option_first",
               "https://doc.rust-lang.org/std/iter/trait.Iterator.html#method.max_by", "2026-10-07")
CBM_STD_LEGACY("Iterator", "min_by", "iter_option_first",
               "https://doc.rust-lang.org/std/iter/trait.Iterator.html#method.min_by", "2026-10-07")
CBM_STD_LEGACY("Iterator", "max_by_key", "iter_option_first",
               "https://doc.rust-lang.org/std/iter/trait.Iterator.html#method.max_by_key",
               "2026-10-07")
CBM_STD_LEGACY("Iterator", "min_by_key", "iter_option_first",
               "https://doc.rust-lang.org/std/iter/trait.Iterator.html#method.min_by_key",
               "2026-10-07")
CBM_STD_LEGACY("Iterator", "rfind", "iter_option_first",
               "https://doc.rust-lang.org/std/iter/trait.Iterator.html#method.rfind", "2026-10-07")
CBM_STD_LEGACY("Iterator", "next_back", "iter_option_first",
               "https://doc.rust-lang.org/std/iter/trait.Iterator.html#method.next_back",
               "2026-10-07")
CBM_STD_LEGACY("Iterator", "by_ref", "iter_ref_receiver",
               "https://doc.rust-lang.org/std/iter/trait.Iterator.html#method.by_ref", "2026-10-07")
CBM_STD_LEGACY("Iterator", "filter", "iter_iterator_args",
               "https://doc.rust-lang.org/std/iter/trait.Iterator.html#method.filter", "2026-10-07")
CBM_STD_LEGACY("Iterator", "take", "iter_iterator_args",
               "https://doc.rust-lang.org/std/iter/trait.Iterator.html#method.take", "2026-10-07")
CBM_STD_LEGACY("Iterator", "skip", "iter_iterator_args",
               "https://doc.rust-lang.org/std/iter/trait.Iterator.html#method.skip", "2026-10-07")
CBM_STD_LEGACY("Iterator", "rev", "iter_iterator_args",
               "https://doc.rust-lang.org/std/iter/trait.Iterator.html#method.rev", "2026-10-07")
CBM_STD_LEGACY("Iterator", "cloned", "iter_iterator_args",
               "https://doc.rust-lang.org/std/iter/trait.Iterator.html#method.cloned", "2026-10-07")
CBM_STD_LEGACY("Iterator", "copied", "iter_iterator_args",
               "https://doc.rust-lang.org/std/iter/trait.Iterator.html#method.copied", "2026-10-07")
CBM_STD_LEGACY("Iterator", "step_by", "iter_iterator_args",
               "https://doc.rust-lang.org/std/iter/trait.Iterator.html#method.step_by",
               "2026-10-07")
CBM_STD_LEGACY("Iterator", "fuse", "iter_iterator_args",
               "https://doc.rust-lang.org/std/iter/trait.Iterator.html#method.fuse", "2026-10-07")
CBM_STD_LEGACY("Iterator", "peekable", "iter_iterator_args",
               "https://doc.rust-lang.org/std/iter/trait.Iterator.html#method.peekable",
               "2026-10-07")
CBM_STD_LEGACY("Iterator", "chain", "iter_iterator_args",
               "https://doc.rust-lang.org/std/iter/trait.Iterator.html#method.chain", "2026-10-07")
CBM_STD_LEGACY("Iterator", "take_while", "iter_iterator_args",
               "https://doc.rust-lang.org/std/iter/trait.Iterator.html#method.take_while",
               "2026-10-07")
CBM_STD_LEGACY("Iterator", "skip_while", "iter_iterator_args",
               "https://doc.rust-lang.org/std/iter/trait.Iterator.html#method.skip_while",
               "2026-10-07")
CBM_STD_LEGACY("Iterator", "inspect", "iter_iterator_args",
               "https://doc.rust-lang.org/std/iter/trait.Iterator.html#method.inspect",
               "2026-10-07")
CBM_STD_LEGACY("Iterator", "sum", "iter_first_generic",
               "https://doc.rust-lang.org/std/iter/trait.Iterator.html#method.sum", "2026-10-07")
CBM_STD_LEGACY("Iterator", "product", "iter_first_generic",
               "https://doc.rust-lang.org/std/iter/trait.Iterator.html#method.product",
               "2026-10-07")
CBM_STD_LEGACY("Iterator", "fold", "iter_first_generic",
               "https://doc.rust-lang.org/std/iter/trait.Iterator.html#method.fold", "2026-10-07")
CBM_STD_LEGACY("Iterator", "reduce", "iter_first_generic",
               "https://doc.rust-lang.org/std/iter/trait.Iterator.html#method.reduce", "2026-10-07")
CBM_STD_LEGACY("Iterator", "any", "iter_bool",
               "https://doc.rust-lang.org/std/iter/trait.Iterator.html#method.any", "2026-10-07")
CBM_STD_LEGACY("Iterator", "all", "iter_bool",
               "https://doc.rust-lang.org/std/iter/trait.Iterator.html#method.all", "2026-10-07")
CBM_STD_LEGACY("Iterator", "position", "iter_option_usize",
               "https://doc.rust-lang.org/std/iter/trait.Iterator.html#method.position",
               "2026-10-07")
CBM_STD_LEGACY("HashMap", "len", "map_usize",
               "https://doc.rust-lang.org/std/collections/struct.HashMap.html#method.len",
               "2026-10-07")
CBM_STD_LEGACY("BTreeMap", "len", "map_usize",
               "https://doc.rust-lang.org/std/collections/struct.BTreeMap.html#method.len",
               "2026-10-07")
CBM_STD_LEGACY("HashMap", "is_empty", "map_bool",
               "https://doc.rust-lang.org/std/collections/struct.HashMap.html#method.is_empty",
               "2026-10-07")
CBM_STD_LEGACY("HashMap", "contains_key", "map_bool",
               "https://doc.rust-lang.org/std/collections/struct.HashMap.html#method.contains_key",
               "2026-10-07")
CBM_STD_LEGACY("BTreeMap", "is_empty", "map_bool",
               "https://doc.rust-lang.org/std/collections/struct.BTreeMap.html#method.is_empty",
               "2026-10-07")
CBM_STD_LEGACY("BTreeMap", "contains_key", "map_bool",
               "https://doc.rust-lang.org/std/collections/struct.BTreeMap.html#method.contains_key",
               "2026-10-07")
CBM_STD_LEGACY("HashMap", "get", "map_option_second",
               "https://doc.rust-lang.org/std/collections/struct.HashMap.html#method.get",
               "2026-10-07")
CBM_STD_LEGACY("HashMap", "get_mut", "map_option_second",
               "https://doc.rust-lang.org/std/collections/struct.HashMap.html#method.get_mut",
               "2026-10-07")
CBM_STD_LEGACY("HashMap", "remove", "map_option_second",
               "https://doc.rust-lang.org/std/collections/struct.HashMap.html#method.remove",
               "2026-10-07")
CBM_STD_LEGACY("BTreeMap", "get", "map_option_second",
               "https://doc.rust-lang.org/std/collections/struct.BTreeMap.html#method.get",
               "2026-10-07")
CBM_STD_LEGACY("BTreeMap", "get_mut", "map_option_second",
               "https://doc.rust-lang.org/std/collections/struct.BTreeMap.html#method.get_mut",
               "2026-10-07")
CBM_STD_LEGACY("BTreeMap", "remove", "map_option_second",
               "https://doc.rust-lang.org/std/collections/struct.BTreeMap.html#method.remove",
               "2026-10-07")
CBM_STD_LEGACY("HashMap", "iter", "map_iterator_pair",
               "https://doc.rust-lang.org/std/collections/struct.HashMap.html#method.iter",
               "2026-10-07")
CBM_STD_LEGACY("HashMap", "iter_mut", "map_iterator_pair",
               "https://doc.rust-lang.org/std/collections/struct.HashMap.html#method.iter_mut",
               "2026-10-07")
CBM_STD_LEGACY("BTreeMap", "iter", "map_iterator_pair",
               "https://doc.rust-lang.org/std/collections/struct.BTreeMap.html#method.iter",
               "2026-10-07")
CBM_STD_LEGACY("BTreeMap", "iter_mut", "map_iterator_pair",
               "https://doc.rust-lang.org/std/collections/struct.BTreeMap.html#method.iter_mut",
               "2026-10-07")
CBM_STD_LEGACY("HashMap", "keys", "map_iterator_first",
               "https://doc.rust-lang.org/std/collections/struct.HashMap.html#method.keys",
               "2026-10-07")
CBM_STD_LEGACY("HashMap", "into_keys", "map_iterator_first",
               "https://doc.rust-lang.org/std/collections/struct.HashMap.html#method.into_keys",
               "2026-10-07")
CBM_STD_LEGACY("BTreeMap", "keys", "map_iterator_first",
               "https://doc.rust-lang.org/std/collections/struct.BTreeMap.html#method.keys",
               "2026-10-07")
CBM_STD_LEGACY("BTreeMap", "into_keys", "map_iterator_first",
               "https://doc.rust-lang.org/std/collections/struct.BTreeMap.html#method.into_keys",
               "2026-10-07")
CBM_STD_LEGACY("HashMap", "values", "map_iterator_second",
               "https://doc.rust-lang.org/std/collections/struct.HashMap.html#method.values",
               "2026-10-07")
CBM_STD_LEGACY("HashMap", "values_mut", "map_iterator_second",
               "https://doc.rust-lang.org/std/collections/struct.HashMap.html#method.values_mut",
               "2026-10-07")
CBM_STD_LEGACY("HashMap", "into_values", "map_iterator_second",
               "https://doc.rust-lang.org/std/collections/struct.HashMap.html#method.into_values",
               "2026-10-07")
CBM_STD_LEGACY("BTreeMap", "values", "map_iterator_second",
               "https://doc.rust-lang.org/std/collections/struct.BTreeMap.html#method.values",
               "2026-10-07")
CBM_STD_LEGACY("BTreeMap", "values_mut", "map_iterator_second",
               "https://doc.rust-lang.org/std/collections/struct.BTreeMap.html#method.values_mut",
               "2026-10-07")
CBM_STD_LEGACY("BTreeMap", "into_values", "map_iterator_second",
               "https://doc.rust-lang.org/std/collections/struct.BTreeMap.html#method.into_values",
               "2026-10-07")
CBM_STD_LEGACY("Iterator", "enumerate", "iter_enumerate",
               "https://doc.rust-lang.org/std/iter/trait.Iterator.html#method.enumerate",
               "2026-10-07")
CBM_STD_LEGACY("Iterator", "map", "adapter_map",
               "https://doc.rust-lang.org/std/iter/trait.Iterator.html#method.map", "2026-10-07")
CBM_STD_LEGACY("Iterator", "filter_map", "adapter_filter_map",
               "https://doc.rust-lang.org/std/iter/trait.Iterator.html#method.filter_map",
               "2026-10-07")
CBM_STD_LEGACY("Iterator", "find_map", "adapter_find_map",
               "https://doc.rust-lang.org/std/iter/trait.Iterator.html#method.find_map",
               "2026-10-07")
CBM_STD_LEGACY("Iterator", "flatten", "adapter_flatten",
               "https://doc.rust-lang.org/std/iter/trait.Iterator.html#method.flatten",
               "2026-10-07")
CBM_STD_LEGACY("Iterator", "flat_map", "adapter_flat_map",
               "https://doc.rust-lang.org/std/iter/trait.Iterator.html#method.flat_map",
               "2026-10-07")
CBM_STD_LEGACY("Iterator", "zip", "adapter_zip",
               "https://doc.rust-lang.org/std/iter/trait.Iterator.html#method.zip", "2026-10-07")
#endif
