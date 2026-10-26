mod query;

use query::Query;

fn total(q: &Query) -> u32 {
    let mut sum = 0;
    for x in q.iter() {
        sum += u32::from(*x);
    }
    sum
}

fn bump(q: &mut Query) {
    for x in q.iter_mut() {
        *x += 1;
    }
}

fn main() {
    let mut q = Query::new(vec![1, 2]);
    bump(&mut q);
    println!("{}\n{}", total(&q), std::backtrace::Backtrace::force_capture());
}
