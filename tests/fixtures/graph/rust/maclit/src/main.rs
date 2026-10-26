//! A textual-scope macro whose arms bind string literals: `$msg:literal` and `tt`
//! must consume `"…"`, raw `r#"…"#` and byte strings as single tokens.

macro_rules! expect_eq {
    ($left:expr, $right:expr, $msg:literal) => {
        if $left != $right {
            panic!("{}", $msg)
        }
    };
}

macro_rules! labelled {
    ($label:tt => $e:expr) => {
        ($label, $e)
    };
}

macro_rules! pick {
    ($k:literal, $msg:literal, $e:expr) => {
        $e
    };
    ($k:ident, $msg:literal, $e:expr) => {
        stringify!($e)
    };
}

fn picked() -> &'static str { println!("picked\n{}", std::backtrace::Backtrace::force_capture()); "picked" }

// The transcriber's own `v` is invisible to the captured `$e` (hygiene).
macro_rules! scoped {
    ($e:expr) => {{
        let v: crate::B = crate::B;
        let _ = &v;
        $e
    }};
}

// Struct patterns bind `v` too: the captured `v.go()` still means A::go.
macro_rules! destructured {
    ($e:expr) => {{
        let crate::W { field: v } = crate::W { field: crate::B };
        let _ = &v;
        $e
    }};
}

macro_rules! arm_bound {
    ($e:expr) => {
        match (crate::W { field: crate::B }) {
            crate::W { field: v } => {
                let _ = &v;
                $e
            }
        }
    };
}

// The scrutinee is outside the arm's binders: the captured call is the caller's.
macro_rules! scrutinised {
    ($e:expr) => {
        match (&$e,) {
            (left,) => {
                let _ = left;
            }
        }
    };
}

// `0..10` is two literals around `..`: rustc takes the first arm.
macro_rules! span {
    ($lo:literal .. $hi:literal, $e:expr) => {
        stringify!($e)
    };
    ($r:expr, $e:expr) => {
        $e
    };
}

// `1.` is one float literal: rustc takes the literal arm.
macro_rules! float_first {
    ($l:literal, $e:expr) => {
        stringify!($e)
    };
    ($r:expr, $e:expr) => {
        $e
    };
}

// The caller's names bound by the transcriber's `let`: `v` becomes B here.
macro_rules! rebind {
    ($p:ident, $q:ident, $e:expr) => {{
        let $p = crate::B;
        let $q = $p;
        $e
    }};
}

// `if let` binds `v` in the consequence only; `else` sees the caller's `v`.
macro_rules! if_let_else {
    ($e:expr) => {
        if let Some(v) = None::<crate::B> {
            let _ = &v;
        } else {
            $e
        }
    };
}

struct W {
    field: B,
}

struct A;
struct B;
impl A { fn go(&self) { println!("A::go\n{}", std::backtrace::Backtrace::force_capture()); } }
impl B { fn go(&self) { println!("B::go\n{}", std::backtrace::Backtrace::force_capture()); } }

fn hygiene() {
    let v = A;
    scoped!(v.go());
}

fn struct_let() {
    let v = A;
    destructured!(v.go());
}

fn struct_arm() {
    let v = A;
    arm_bound!(v.go());
}

fn spanned() {
    println!("{}", span!(0..10, four()));
}

fn trailing_dot() {
    println!("{}", float_first!(1., four()));
}

fn rebound() {
    let v = A;
    let _ = &v;
    rebind!(v, y, y.go());
}

fn else_branch() {
    let v = A;
    if_let_else!(v.go());
}

fn scrutinee() {
    scrutinised!(four());
}

fn four() -> u32 { println!("four\n{}", std::backtrace::Backtrace::force_capture()); 4 }
fn name(s: &str) -> usize { println!("name\n{}", std::backtrace::Backtrace::force_capture()); s.len() }
fn bytes() -> u8 { println!("bytes\n{}", std::backtrace::Backtrace::force_capture()); 1 }

fn literal_arm() {
    expect_eq!(four(), 4, "four, as written");
    expect_eq!(name("a,b"), 3, r#"a "quoted", raw message"#);
}

fn tt_arm() {
    let pair = labelled!(b"tag" => bytes());
    println!("{:?}", pair);
}

fn ident_is_no_literal() {
    let _ = pick!(tag, "message", picked());
}

fn literal_arm_taken() {
    let _ = pick!(-1, "message", picked());
}

// A `ty` fragment ends before the `[` that follows the type.
macro_rules! typed_list {
    ($t:ty [$($e:expr),*]) => {
        [$($e as $t),*]
    };
}

fn listed() -> u8 { println!("listed\n{}", std::backtrace::Backtrace::force_capture()); 1 }

fn type_then_bracket() {
    let _ = typed_list!(u32 [listed(), listed()]);
}

// A reference with a lifetime and a function type with an arrow are not
// complete before `[`: the `ty` arm takes the whole type and drops the call.
macro_rules! choose_ref {
    ($t:ty, $e:expr) => { () };
    (&'static [u8], $e:expr) => { $e };
}

macro_rules! choose_fn {
    ($t:ty, $e:expr) => { () };
    (fn() -> [u8; 1], $e:expr) => { $e };
}

fn dropped_ref() -> u8 { println!("dropped_ref\n{}", std::backtrace::Backtrace::force_capture()); 1 }
fn dropped_fn() -> u8 { println!("dropped_fn\n{}", std::backtrace::Backtrace::force_capture()); 1 }

fn type_arm_taken() {
    let _ = choose_ref!(&'static [u8], dropped_ref());
    let _ = choose_fn!(fn() -> [u8; 1], dropped_fn());
}

fn main() {
    type_arm_taken();
    type_then_bracket();
    ident_is_no_literal();
    literal_arm_taken();
    hygiene();
    struct_let();
    struct_arm();
    scrutinee();
    trailing_dot();
    rebound();
    else_branch();
    spanned();
    B.go();
    literal_arm();
    tt_arm();
}
