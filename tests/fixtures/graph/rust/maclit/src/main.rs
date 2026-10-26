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

fn main() {
    ident_is_no_literal();
    literal_arm_taken();
    hygiene();
    struct_let();
    struct_arm();
    scrutinee();
    B.go();
    literal_arm();
    tt_arm();
}
