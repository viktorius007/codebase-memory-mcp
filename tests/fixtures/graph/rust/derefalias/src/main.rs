use core::ops::Deref;

pub struct Graph<const DIRECTED: bool, N> {
    nodes: Vec<N>,
}

impl<const DIRECTED: bool, N> Graph<DIRECTED, N> {
    pub fn node_count(&self) -> usize {
        println!("Graph::node_count\n{}", std::backtrace::Backtrace::force_capture());
        self.nodes.len()
    }
}

pub type DiGraph<N> = Graph<true, N>;

pub struct Dag<N> {
    graph: DiGraph<N>,
}

impl<N> Deref for Dag<N> {
    type Target = DiGraph<N>;

    fn deref(&self) -> &DiGraph<N> {
        &self.graph
    }
}

fn count(dag: &Dag<u32>) -> usize {
    dag.node_count()
}

fn main() {
    let dag = Dag { graph: Graph { nodes: vec![1, 2] } };
    println!("{}\n{}", count(&dag), std::backtrace::Backtrace::force_capture());
}
