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

fn main() {
    literal_arm();
    tt_arm();
}
