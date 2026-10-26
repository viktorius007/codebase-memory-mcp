use crate::query::{QueryData, QueryFilter};
use crate::system::Query;

pub trait SystemParam {
    type Item<'w, 's>;
}

pub type SystemParamItem<'w, 's, P> = <P as SystemParam>::Item<'w, 's>;

impl<D: QueryData + 'static, F: QueryFilter + 'static> SystemParam for Query<'_, '_, D, F> {
    type Item<'w, 's> = Query<'w, 's, D, F>;
}
