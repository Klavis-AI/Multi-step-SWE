/**
 * Created by HH on 2017/3/16.
 */
import {createSlice} from '@reduxjs/toolkit';

import api from '../store/api';
import initialState from '../options/sellersOption';
import {writeIn} from './writeIn';

const sellersSlice = createSlice({
  name: 'sellers',
  initialState,
  reducers: {},
  extraReducers: (builder) => {
    builder
      .addMatcher(api.endpoints.sellersList.matchFulfilled, (state, action) => {
        const list = [];
        action.payload.forEach((item) => {
          list.push(item.shopName);
        });
        writeIn(state, ['sellers'], list);
      })
      .addMatcher(api.endpoints.allSellersLineChart.matchFulfilled, (state, action) => {
        const obj = {seller: [], num: [], percent: [], lastPage: false};
        action.payload.forEach((item) => {
          if (item.isLastPage) {
            obj.lastPage = true;
            obj.seller.push(item.shopName);
            obj.num.push(item.incrementNum);
            obj.percent.push(item.momPercent);
          } else {
            obj.seller.push(item.shopName);
            obj.num.push(item.incrementNum);
            obj.percent.push(item.momPercent);
          }
        });
        writeIn(state, ['lineAndBar', 'xAxis', 0, 'data'], obj.seller);
        writeIn(state, ['lineAndBar', 'xAxis', 0, 'lastPage'], obj.lastPage);
        writeIn(state, ['lineAndBar', 'series', 0, 'data'], obj.num);
        writeIn(state, ['lineAndBar', 'series', 1, 'data'], obj.percent);
      })
      .addMatcher(api.endpoints.singleSellerCustomerNumInit.matchFulfilled, (state, action) => {
        const yesterday = {};
        const customer = {name: action.payload[0].shopName, time: [], num1: [], num2: [], percent: []};
        /* The payload is read back to front.  It used to be reversed in place,
           which is no longer available: the array handed to a reducer now is the
           cached response, and reversing it would reorder every later reader of
           the same response as well. */
        [...action.payload].reverse().forEach((item) => {
          if (item.yesterdayMaxNumber > 0) {
            yesterday.num = item.yesterdayMaxNumber;
            yesterday.time = item.maxNumberTime.split(' ')[1];
          } else {
            customer.time.push(item.countDate.split(' ')[1]);
            customer.num1.push(item.customerNumber);
            customer.num2.push(item.doorCustomerNumber);
            customer.percent.push(item.customerRatio);
          }
        });
        writeIn(state, ['customerNum', 'xAxis', 0, 'data'], customer.time);
        writeIn(state, ['customerNum', 'series', 0, 'data'], customer.num1);
        writeIn(state, ['customerNum', 'series', 1, 'data'], customer.num2);
        writeIn(state, ['customerNum', 'series', 2, 'data'], customer.percent);
        writeIn(state, ['customerNum', 'name'], customer.name);
        writeIn(state, ['customerNum', 'xAxis', 0, 'yesterday'], yesterday);
      })
      .addMatcher(api.endpoints.singleSellerCustomerNumFetch.matchFulfilled, (state, action) => {
        // 新的一条进队首，最老的一条出队尾
        const shift = (path, value) => {
          let node = state;
          for (const key of path) {
            node = node[key];
          }
          const next = node.slice();
          next.unshift(value);
          next.pop();
          writeIn(state, path, next);
        };
        shift(['customerNum', 'xAxis', 0, 'data'], action.payload.countDate.split(' ')[1]);
        shift(['customerNum', 'series', 0, 'data'], action.payload.customerNumber);
        shift(['customerNum', 'series', 1, 'data'], action.payload.doorCustomerNumber);
        shift(['customerNum', 'series', 2, 'data'], action.payload.customerRatio);
      })
      .addMatcher(api.endpoints.singleSellerCustomerFlowInit.matchFulfilled, (state, action) => {
        const CustomerFlow = {name: action.payload[0].shopName, time: [], num: [], percent: []};
        action.payload.forEach((item) => {
          if (item.countDate.split(' ')[1]) {
            CustomerFlow.time.push(item.countDate.split(' ')[1]);
            CustomerFlow.num.push(item.customerNumber);
            CustomerFlow.percent.push(item.customerRatio);
          } else {
            CustomerFlow.time.push(item.countDate);
            CustomerFlow.num.push(item.customerNumber);
            CustomerFlow.percent.push(item.customerRatio);
          }
        });
        writeIn(state, ['customerFlow', 'xAxis', 'data'], CustomerFlow.time);
        writeIn(state, ['customerFlow', 'name'], CustomerFlow.name);
        writeIn(state, ['customerFlow', 'series', 0, 'data'], CustomerFlow.num);
        writeIn(state, ['customerFlow', 'series', 1, 'data'], CustomerFlow.percent);
      })
      .addMatcher(api.endpoints.singleSellerCustomerAvgInit.matchFulfilled, (state, action) => {
        const CustomerAvg = {name: action.payload[0].shopName, time: [], num: [], percent: []};
        action.payload.forEach((item) => {
          if (item.countDate.split(' ')[1]) {
            CustomerAvg.time.push(item.countDate.split(' ')[1]);
            CustomerAvg.num.push(item.customerNumber);
            CustomerAvg.percent.push(item.customerRatio);
          } else {
            CustomerAvg.time.push(item.countDate);
            CustomerAvg.num.push(item.customerNumber);
            CustomerAvg.percent.push(item.customerRatio);
          }
        });
        writeIn(state, ['customerAvg', 'xAxis', 'data'], CustomerAvg.time);
        writeIn(state, ['customerAvg', 'name'], CustomerAvg.name);
        writeIn(state, ['customerAvg', 'series', 0, 'data'], CustomerAvg.num);
        writeIn(state, ['customerAvg', 'series', 1, 'data'], CustomerAvg.percent);
      })
      .addMatcher(api.endpoints.singleSellerCustomerInInit.matchFulfilled, (state, action) => {
        const customerIn = {name: '', time: [], num: [], percent1: [], percent2: []};
        customerIn.name = action.payload.name;
        action.payload.forEach((item) => {
          if (item.countDate.split(' ')[1]) {
            customerIn.time.push(item.countDate.split(' ')[1]);
            customerIn.num.push(item.incrementNum);
            customerIn.percent1.push(item.incrementRatio);
          } else {
            customerIn.time.push(item.countDate);
            customerIn.num.push(item.incrementNum);
            customerIn.percent1.push(item.incrementRatio);
          }
        });
        writeIn(state, ['customerIn', 'xAxis', 'data'], customerIn.time);
        writeIn(state, ['customerIn', 'name'], customerIn.name);
        writeIn(state, ['customerIn', 'series', 0, 'data'], customerIn.num);
        writeIn(state, ['customerIn', 'series', 1, 'data'], customerIn.percent1);
        // writeIn(state,['customerIn','series',2,'data'],customerIn.percent2);
      })
      .addMatcher(api.endpoints.singleSellerRadar.matchFulfilled, (state, action) => {
        const radar = {name: [], value: []};
        action.payload.forEach((item) => {
          radar.name.push(item.indicators);
          radar.value.push(item.score);
        });
        writeIn(state, ['radar', 'series', 'data', 0], radar.value);
        writeIn(state, ['radar', 'radar', 'indicator', 0, 'name'], radar.name[0]);
        writeIn(state, ['radar', 'radar', 'indicator', 1, 'name'], radar.name[1]);
        writeIn(state, ['radar', 'radar', 'indicator', 2, 'name'], radar.name[2]);
        writeIn(state, ['radar', 'radar', 'indicator', 3, 'name'], radar.name[3]);
        writeIn(state, ['radar', 'radar', 'indicator', 4, 'name'], radar.name[4]);
      })
      .addMatcher(api.endpoints.singleSellerStayBar.matchFulfilled, (state, action) => {
        const staybar = {name: [], value: []};
        action.payload.forEach((item) => {
          staybar.name.push(item.key);
          staybar.value.push(item.value);
        });
        writeIn(state, ['stayBar', 'xAxis', 0, 'data'], staybar.name);
        writeIn(state, ['stayBar', 'series', 0, 'data'], staybar.value);
      })
      .addMatcher(api.endpoints.singleSellerOldOrNew.matchFulfilled, (state, action) => {
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
      .addMatcher(api.endpoints.singleSellerTimeSection.matchFulfilled, (state, action) => {
        const timeSection = {name: [], value: []};
        action.payload.forEach((item) => {
          timeSection.name.push(item.key);
          timeSection.value.push(item.value);
        });
        writeIn(state, ['timeSection', 'xAxis', 0, 'data'], timeSection.name);
        writeIn(state, ['timeSection', 'series', 0, 'data'], timeSection.value);
      })
      .addMatcher(api.endpoints.singleSellerDeepVisit.matchFulfilled, (state, action) => {
        const deep = {time: [], deep: [], out: [], deepNum: [], outNum: []};
        action.payload.forEach((item) => {
          deep.time.push(item.countDate);
          deep.deep.push(item.deepRatio);
          deep.deepNum.push(item.deepNum);
          deep.out.push(item.jumpRatio);
          deep.outNum.push(item.jumpNum);
        });
        writeIn(state, ['deepVisit', 'xAxis', 'data'], deep.time);
        writeIn(state, ['deepVisit', 'series', 0, 'data'], deep.deep);
        writeIn(state, ['deepVisit', 'series', 1, 'data'], deep.out);
        writeIn(state, ['deepVisit', 'xAxis', 'deepNum'], deep.deepNum);
        writeIn(state, ['deepVisit', 'xAxis', 'outNum'], deep.outNum);
      })
      .addMatcher(api.endpoints.singleSellerCycleInit.matchFulfilled, (state, action) => {
        const cycle = {name: [], value: []};
        action.payload.forEach((item) => {
          cycle.name.push(item.key);
          cycle.value.push(item.value);
        });
        writeIn(state, ['cycle', 'xAxis', 0, 'data'], cycle.name);
        writeIn(state, ['cycle', 'series', 0, 'data'], cycle.value);
      })
      .addMatcher(api.endpoints.singleSellerActiveInit.matchFulfilled, (state, action) => {
        const active = {name: [], value: []};
        action.payload.forEach((item) => {
          active.name.push(item.key);
          active.value.push(item.value);
        });
        writeIn(state, ['active', 'xAxis', 0, 'data'], active.name.reverse());
        writeIn(state, ['active', 'series', 0, 'data'], active.value);
      });
  }
});

export default sellersSlice.reducer;
