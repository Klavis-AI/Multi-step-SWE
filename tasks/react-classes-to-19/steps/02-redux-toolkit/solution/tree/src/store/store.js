import {configureStore} from '@reduxjs/toolkit';

import api from './api';
import reducers from '../reducers/index';

/* The four slices keep the four sections of state the old store had, under
   names rather than the a/b/c/d keys `combineReducers` was given; the query
   cache sits beside them under the api's own reducerPath.  Each slice's initial
   state is the echarts option it always started from - the old store passed the
   same objects to createStore as preloaded state, which is why every reducer's
   own `Immutable.Map({})` default was dead code. */
const store = configureStore({
  reducer: {
    firstPage: reducers.firstPage,
    sellers: reducers.sellers,
    statistics: reducers.statistics,
    compare: reducers.compare,
    [api.reducerPath]: api.reducer
  },
  middleware: getDefaultMiddleware => getDefaultMiddleware().concat(api.middleware)
});

export default store;
