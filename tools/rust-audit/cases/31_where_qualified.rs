pub fn f<I: Iterator>() where <I as Iterator>::Item: Send, [I::Item; 2]: Send, for<'a> &'a I: Send, dyn Send: Send {}
