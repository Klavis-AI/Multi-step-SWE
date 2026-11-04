import {createSlice} from '@reduxjs/toolkit';

import api from '../store/api';
import initialState from '../options/statisticsOption';
import {writeIn} from './writeIn';

const statisticsSlice = createSlice({
  name: 'statistics',
  initialState,
  reducers: {},
  extraReducers: (builder) => {
    builder
      .addMatcher(api.endpoints.statisticsCustomerNumInit.matchFulfilled, (state, action) => {
        const obj = {time: [], num: [], percent: []};
        action.payload.forEach((item) => {
          obj.time.push(item.countDate);
          obj.num.push(item.incrementNum);
          // obj.percent.push(item.percent);
        });
        writeIn(state, ['customerNum', 'xAxis', 0, 'data'], obj.time);
        writeIn(state, ['customerNum', 'series', 0, 'data'], obj.num);
        // writeIn(state,['customerNum','series',1,'data'],obj.percent);
      })
      .addMatcher(api.endpoints.statisticsPeakInit.matchFulfilled, (state, action) => {
        const peak = {time: [], value: []};
        action.payload.forEach((item) => {
          if (item.countDate.split(' ')[1]) {
            peak.time.push(item.countDate.split(' ')[1]);
            peak.value.push(item.customerNumber);
          } else {
            peak.time.push(item.countDate);
            peak.value.push(item.customerNumber);
          }
        });
        writeIn(state, ['customerPeak', 'xAxis', 0, 'data'], peak.time);
        writeIn(state, ['customerPeak', 'series', 0, 'data'], peak.value);
      })
      .addMatcher(api.endpoints.statisticsAvgInit.matchFulfilled, (state, action) => {
        const avg = {time: [], value: []};
        action.payload.forEach((item) => {
          if (item.countDate.split(' ')[1]) {
            avg.time.push(item.countDate.split(' ')[1]);
            avg.value.push(item.customerNumber);
          } else {
            avg.time.push(item.countDate);
            avg.value.push(item.customerNumber);
          }
        });
        writeIn(state, ['customerAvg', 'xAxis', 0, 'data'], avg.time);
        writeIn(state, ['customerAvg', 'series', 0, 'data'], avg.value);
      })
      .addMatcher(api.endpoints.statisticsDeepInit.matchFulfilled, (state, action) => {
        const deep = {time: [], deepNum: [], deepRatio: []};
        action.payload.forEach((item) => {
          deep.time.push(item.countDate);
          deep.deepNum.push(item.deepNum);
          deep.deepRatio.push(item.deepRatio);
        });
        writeIn(state, ['deep', 'xAxis', 0, 'data'], deep.time);
        writeIn(state, ['deep', 'series', 0, 'data'], deep.deepRatio);
        writeIn(state, ['deep', 'xAxis', 0, 'num'], deep.deepNum);
      })
      .addMatcher(api.endpoints.statisticsOutInit.matchFulfilled, (state, action) => {
        const out = {time: [], jumpNum: [], jumpRatio: []};
        action.payload.forEach((item) => {
          out.time.push(item.countDate);
          out.jumpNum.push(item.jumpNum);
          out.jumpRatio.push(item.jumpRatio);
        });
        writeIn(state, ['out', 'xAxis', 0, 'data'], out.time);
        writeIn(state, ['out', 'series', 0, 'data'], out.jumpRatio);
        writeIn(state, ['out', 'xAxis', 0, 'num'], out.jumpNum);
      })
      .addMatcher(api.endpoints.statisticsOldOrNewInit.matchFulfilled, (state, action) => {
        const oldnew = {time: [], old: [], new: []};
        action.payload.forEach((item) => {
          oldnew.time.push(item.countDate);
          oldnew.old.push(item.oldCustomer);
          oldnew.new.push(item.newCustomer);
        });
        writeIn(state, ['oldOrNew', 'xAxis', 0, 'data'], oldnew.time);
        writeIn(state, ['oldOrNew', 'series', 0, 'data'], oldnew.new);
        writeIn(state, ['oldOrNew', 'series', 1, 'data'], oldnew.old);
      })
      .addMatcher(api.endpoints.statisticsTimeSectionInit.matchFulfilled, (state, action) => {
        const timeSec = {key: [], value: []};
        action.payload.forEach((item) => {
          timeSec.key.push(item.key);
          timeSec.value.push(item.value);
        });
        writeIn(state, ['timeSection', 'xAxis', 0, 'data'], timeSec.key);
        writeIn(state, ['timeSection', 'series', 0, 'data'], timeSec.value);
      })
      .addMatcher(api.endpoints.statisticsCycleInit.matchFulfilled, (state, action) => {
        const cycle = {name: [], value: []};
        action.payload.forEach((item) => {
          cycle.name.push(item.key);
          cycle.value.push(item.value);
        });
        writeIn(state, ['cycle', 'xAxis', 0, 'data'], cycle.name);
        writeIn(state, ['cycle', 'series', 0, 'data'], cycle.value);
      })
      .addMatcher(api.endpoints.statisticsActiveInit.matchFulfilled, (state, action) => {
        const active = {name: [], value: []};
        action.payload.forEach((item) => {
          active.name.push(item.key);
          active.value.push(item.value);
        });
        writeIn(state, ['active', 'xAxis', 0, 'data'], active.name.reverse());
        writeIn(state, ['active', 'series', 0, 'data'], active.value);
      })
      .addMatcher(api.endpoints.statisticsStayInit.matchFulfilled, (state, action) => {
        const stay = {name: [], value: []};
        action.payload.forEach((item) => {
          stay.name.push(item.key);
          stay.value.push(item.value);
        });
        writeIn(state, ['stay', 'xAxis', 0, 'data'], stay.name.reverse());
        writeIn(state, ['stay', 'series', 0, 'data'], stay.value);
      });
  }
});

export default statisticsSlice.reducer;
