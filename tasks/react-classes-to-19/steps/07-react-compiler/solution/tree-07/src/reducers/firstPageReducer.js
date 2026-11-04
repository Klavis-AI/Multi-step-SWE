import {createSlice} from '@reduxjs/toolkit';

import api from '../store/api';
import initialState from '../options/firstPageOption';
import {writeIn} from './writeIn';

/* 首页.  Each case that used to be a `case TYPE.x` on a dispatched action is now
   matched on the fulfilled response of the endpoint that replaced the thunk;
   the body is the same transformation of the same payload. */
const firstPageSlice = createSlice({
  name: 'firstPage',
  initialState,
  reducers: {},
  extraReducers: (builder) => {
    builder
      .addMatcher(api.endpoints.firstPageNumInit.matchFulfilled, (state, action) => {
        const numInit = {time: [], value: [], percent: []};
        const yesterday = {};

        action.payload.forEach((item) => {
          if (item.yesterdayMaxNumber > 0) {
            yesterday.num = item.yesterdayMaxNumber;
            yesterday.countDate = item.countDate.split(' ')[1];
          } else {
            numInit.time.push(item.countDate.split(' ')[1]);
            numInit.value.push(item.customerNumber);
          }
        });
        writeIn(state, ['line', 'xAxis', 'data'], numInit.time);
        writeIn(state, ['line', 'series', 0, 'data'], numInit.value);
        // writeIn(state,['line','series',1,'data'],numInit.percent);
        writeIn(state, ['line', 'xAxis', 'yesterday'], yesterday);
      })
      .addMatcher(api.endpoints.firstPageFetch.matchFulfilled, (state, action) => {
        // 一条新数据进队尾，最老的一条出队首，长度不变
        const time = state.line.xAxis.data.slice();
        time.push(action.payload.countDate.split(' ')[1]);
        time.shift();
        const value = state.line.series[0].data.slice();
        value.push(parseInt(action.payload.customerNumber));
        value.shift();
        writeIn(state, ['line', 'xAxis', 'data'], time);
        writeIn(state, ['line', 'series', 0, 'data'], value);
      })
      .addMatcher(api.endpoints.firstPageSellersInit.matchFulfilled, (state, action) => {
        const obj = {sellers: [], num: []};
        action.payload.forEach((item) => {
          obj.sellers.push(item.seller);
          obj.num.push(item.num);
        });
        writeIn(state, ['sellers', 'xAxis', 0, 'data'], obj.sellers);
        writeIn(state, ['sellers', 'series', 0, 'data'], obj.num);
      });
  }
});

export default firstPageSlice.reducer;
