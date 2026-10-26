//! Calls in the arguments of `children!` nested five deep resolve at every
//! level; each level calls its own function, so each level is its own site.
use eco::children;

struct Label(&'static str);

impl Label {
    fn new(text: &'static str) -> Label { println!("new\n{}", std::backtrace::Backtrace::force_capture()); Label(text) }
}

fn level2(v: f32) -> f32 { println!("level2\n{}", std::backtrace::Backtrace::force_capture()); v }
fn level3(v: f32) -> f32 { println!("level3\n{}", std::backtrace::Backtrace::force_capture()); v }
fn level4(v: f32) -> f32 { println!("level4\n{}", std::backtrace::Backtrace::force_capture()); v }
fn level5(v: f32) -> f32 { println!("level5\n{}", std::backtrace::Backtrace::force_capture()); v }
fn in_format(v: f32) -> f32 { println!("in_format\n{}", std::backtrace::Backtrace::force_capture()); v }

fn tree() {
    let _ = children![(
        Label::new("1"),
        children![(
            level2(2.0),
            children![(
                level3(3.0),
                children![(
                    level4(4.0),
                    children![(level5(5.0), format!("{}", in_format(5.0)))]
                )]
            )]
        )]
    )];
}

fn main() {
    tree();
}
