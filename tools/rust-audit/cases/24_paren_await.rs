pub async fn f(mut x: core::pin::Pin<Box<dyn core::future::Future<Output = ()>>>) { (&mut x).await; }
