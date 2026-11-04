import {createSlice} from '@reduxjs/toolkit';

import api from '../store/api';
import initialState from '../options/compareOption';
import {writeIn} from './writeIn';

/* 对比.  Every response here is a pair of series - 时间一 and 时间二, or 商家一 and
   商家二 - which the server sends as an object whose two values are the two
   halves; `value` below is that object read out in order, exactly as before. */
const compareSlice = createSlice({
  name: 'compare',
  initialState,
  reducers: {},
  extraReducers: (builder) => {
    builder
      .addMatcher(api.endpoints.compareNumInit.matchFulfilled, (state, action) => {
        const obj = {time: [], num1: [], num2: []};
        const value = [];
        for (const i in action.payload) {
          value.push(action.payload[i]);
        }
        value[0].forEach((item) => {
          obj.time.push(item.countDate);
          obj.num1.push(item.customerNumber);
        });
        value[1].forEach((item, index) => {
          obj.time[index] = `${obj.time[index]}/${item.countDate}`;
          obj.num2.push(item.customerNumber);
        });
        writeIn(state, ['customerNum', 'xAxis', 0, 'data'], obj.time);
        writeIn(state, ['customerNum', 'series', 0, 'data'], obj.num1);
        writeIn(state, ['customerNum', 'series', 1, 'data'], obj.num2);
      })
      .addMatcher(api.endpoints.compareNumAvgInit.matchFulfilled, (state, action) => {
        const obj = {time: [], num1: [], num2: []};
        const value = [];
        for (const i in action.payload) {
          value.push(action.payload[i]);
        }
        value[0].forEach((item) => {
          obj.time.push(item.countDate);
          obj.num1.push(item.customerNumber);
        });
        value[1].forEach((item, index) => {
          obj.time[index] = `${obj.time[index]}/${item.countDate}`;
          obj.num2.push(item.customerNumber);
        });
        writeIn(state, ['customerAvg', 'xAxis', 0, 'data'], obj.time);
        writeIn(state, ['customerAvg', 'series', 0, 'data'], obj.num1);
        writeIn(state, ['customerAvg', 'series', 1, 'data'], obj.num2);
      })
      .addMatcher(api.endpoints.compareNumInInit.matchFulfilled, (state, action) => {
        const obj = {time: [], num1: [], num2: []};
        const value = [];
        for (const i in action.payload) {
          value.push(action.payload[i]);
        }
        value[0].forEach((item) => {
          obj.time.push(item.countDate);
          obj.num1.push(item.incrementNum);
        });
        value[1].forEach((item, index) => {
          obj.time[index] = `${obj.time[index]}/${item.countDate}`;
          obj.num2.push(item.incrementNum);
        });
        writeIn(state, ['customerIn', 'xAxis', 0, 'data'], obj.time);
        writeIn(state, ['customerIn', 'series', 0, 'data'], obj.num1);
        writeIn(state, ['customerIn', 'series', 1, 'data'], obj.num2);
      })
      .addMatcher(api.endpoints.compareOldOrNewInit.matchFulfilled, (state, action) => {
        const obj = {time: [], num1Old: [], num1New: [], percent1: [], num2Old: [], num2New: [], percent2: []};
        const value = [];
        let total = 0;
        for (const i in action.payload) {
          value.push(action.payload[i]);
        }
        value[0].forEach((item) => {
          obj.time.push(item.countDate);
          obj.num1Old.push(item.oldCustomer);
          obj.num1New.push(item.newCustomer);
          total = parseInt(item.oldCustomer) + parseInt(item.newCustomer);
          if (!total) {
            obj.percent1.push(0);
          } else {
            obj.percent1.push(parseInt(item.newCustomer / total * 100));
          }
        });
        value[1].forEach((item, index) => {
          obj.time[index] = `${obj.time[index]}/${item.countDate}`;
          obj.num2Old.push(item.oldCustomer);
          obj.num2New.push(item.newCustomer);
          total = parseInt(item.oldCustomer) + parseInt(item.newCustomer);
          if (!total) {
            obj.percent2.push(0);
          } else {
            obj.percent2.push(parseInt(item.newCustomer / total * 100));
          }
        });
        writeIn(state, ['oldOrNew', 'xAxis', 0, 'data'], obj.time);
        writeIn(state, ['oldOrNew', 'series', 0, 'data'], obj.percent1);
        writeIn(state, ['oldOrNew', 'series', 1, 'data'], obj.percent2);
        writeIn(state, ['oldOrNew', 'xAxis', 0, 'num1Old'], obj.num1Old);
        writeIn(state, ['oldOrNew', 'xAxis', 0, 'num1New'], obj.num1New);
        writeIn(state, ['oldOrNew', 'xAxis', 0, 'num2Old'], obj.num2Old);
        writeIn(state, ['oldOrNew', 'xAxis', 0, 'num2New'], obj.num2New);
      })
      .addMatcher(api.endpoints.compareDeepVisitInit.matchFulfilled, (state, action) => {
        const obj = {time: [], deep1Num: [], deep2Num: [], deepRatio1: [], deepRatio2: []};
        const value = [];
        for (const i in action.payload) {
          value.push(action.payload[i]);
        }
        value[0].forEach((item) => {
          obj.time.push(item.countDate);
          obj.deep1Num.push(item.deepNum);
          obj.deepRatio1.push(item.deepRatio);
        });
        value[1].forEach((item, index) => {
          obj.time[index] = `${obj.time[index]}/${item.countDate}`;
          obj.deep2Num.push(item.deepNum);
          obj.deepRatio2.push(item.deepRatio);
        });
        writeIn(state, ['deepVisit', 'xAxis', 0, 'data'], obj.time);
        writeIn(state, ['deepVisit', 'series', 0, 'data'], obj.deepRatio1);
        writeIn(state, ['deepVisit', 'series', 1, 'data'], obj.deepRatio2);
        writeIn(state, ['deepVisit', 'xAxis', 0, 'deep1Num'], obj.deep1Num);
        writeIn(state, ['deepVisit', 'xAxis', 0, 'deep2Num'], obj.deep2Num);
      })
      .addMatcher(api.endpoints.compareOutInit.matchFulfilled, (state, action) => {
        const obj = {time: [], out1Num: [], out2Num: [], outRatio1: [], outRatio2: []};
        const value = [];
        for (const i in action.payload) {
          value.push(action.payload[i]);
        }
        value[0].forEach((item) => {
          obj.time.push(item.countDate);
          obj.out1Num.push(item.jumpNum);
          obj.outRatio1.push(item.jumpRatio);
        });
        value[1].forEach((item, index) => {
          obj.time[index] = `${obj.time[index]}/${item.countDate}`;
          obj.out2Num.push(item.jumpNum);
          obj.outRatio2.push(item.jumpRatio);
        });
        writeIn(state, ['out', 'xAxis', 0, 'data'], obj.time);
        writeIn(state, ['out', 'series', 0, 'data'], obj.outRatio1);
        writeIn(state, ['out', 'series', 1, 'data'], obj.outRatio2);
        writeIn(state, ['out', 'xAxis', 0, 'out1Num'], obj.out1Num);
        writeIn(state, ['out', 'xAxis', 0, 'out2Num'], obj.out2Num);
      })
      .addMatcher(api.endpoints.compareActiveInit.matchFulfilled, (state, action) => {
        const obj = {key: [], num1: [], num2: []};
        const value = [];
        for (const i in action.payload) {
          value.push(action.payload[i]);
        }
        value[0].forEach((item) => {
          obj.key.push(item.key);
          obj.num1.push(item.value);
        });
        value[1].forEach((item) => {
          obj.num2.push(item.value);
        });
        writeIn(state, ['active', 'xAxis', 0, 'data'], obj.key);
        writeIn(state, ['active', 'series', 0, 'data'], obj.num1);
        writeIn(state, ['active', 'series', 1, 'data'], obj.num2);
      })
      .addMatcher(api.endpoints.compareCycleInit.matchFulfilled, (state, action) => {
        const obj = {key: [], value1: [], value2: []};
        const value = [];
        for (const i in action.payload) {
          value.push(action.payload[i]);
        }
        value[0].forEach((item) => {
          obj.key.push(item.key);
          obj.value1.push(item.value);
        });
        value[1].forEach((item) => {
          obj.value2.push(item.value);
        });
        writeIn(state, ['cycle', 'xAxis', 0, 'data'], obj.key);
        writeIn(state, ['cycle', 'series', 0, 'data'], obj.value1);
        writeIn(state, ['cycle', 'series', 1, 'data'], obj.value2);
      })
      .addMatcher(api.endpoints.compareStayInit.matchFulfilled, (state, action) => {
        const obj = {key: [], value1: [], value2: []};
        const value = [];
        for (const i in action.payload) {
          value.push(action.payload[i]);
        }
        value[0].forEach((item) => {
          obj.key.push(item.key);
          obj.value1.push(item.value);
        });
        value[1].forEach((item) => {
          obj.value2.push(item.value);
        });
        writeIn(state, ['stay', 'xAxis', 0, 'data'], obj.key);
        writeIn(state, ['stay', 'series', 0, 'data'], obj.value1);
        writeIn(state, ['stay', 'series', 1, 'data'], obj.value2);
      })
      .addMatcher(api.endpoints.compareSellersNumInit.matchFulfilled, (state, action) => {
        const obj = {time: [], num1: [], percent1: [], num2: [], percent2: []};
        const value = [];
        for (const i in action.payload) {
          value.push(action.payload[i]);
        }
        value[0].forEach((item) => {
          obj.time.push(item.countDate);
          obj.num1.push(item.customerNumber);
          obj.percent1.push(item.customerRatio);
        });
        value[1].forEach((item) => {
          obj.num2.push(item.customerNumber);
          obj.percent2.push(item.customerRatio);
        });

        writeIn(state, ['sellersNum', 'xAxis', 0, 'data'], obj.time);
        writeIn(state, ['sellersNum', 'series', 0, 'data'], obj.num1);
        writeIn(state, ['sellersNum', 'series', 1, 'data'], obj.num2);
        writeIn(state, ['sellersNum', 'xAxis', 0, 'percent1'], obj.percent1);
        writeIn(state, ['sellersNum', 'xAxis', 0, 'percent2'], obj.percent2);
      })
      .addMatcher(api.endpoints.compareSellersAvgInit.matchFulfilled, (state, action) => {
        const obj = {time: [], num1: [], percent1: [], num2: [], percent2: []};
        const value = [];
        for (const i in action.payload) {
          value.push(action.payload[i]);
        }
        value[0].forEach((item) => {
          obj.time.push(item.countDate);
          obj.num1.push(item.customerNumber);
          obj.percent1.push(item.customerRatio);
        });
        value[1].forEach((item) => {
          obj.num2.push(item.customerNumber);
          obj.percent2.push(item.customerRatio);
        });
        writeIn(state, ['sellersAvg', 'xAxis', 0, 'data'], obj.time);
        writeIn(state, ['sellersAvg', 'series', 0, 'data'], obj.num1);
        writeIn(state, ['sellersAvg', 'series', 1, 'data'], obj.num2);
        writeIn(state, ['sellersAvg', 'xAxis', 0, 'percent1'], obj.percent1);
        writeIn(state, ['sellersAvg', 'xAxis', 0, 'percent2'], obj.percent2);
      })
      .addMatcher(api.endpoints.compareSellersList.matchFulfilled, (state, action) => {
        const list = [];
        action.payload.forEach((item) => {
          list.push(item.shopName);
        });
        writeIn(state, ['sellersList'], list);
      })
      .addMatcher(api.endpoints.compareSellersInInit.matchFulfilled, (state, action) => {
        const obj = {time: [], num1: [], num2: []};
        const value = [];
        for (const i in action.payload) {
          value.push(action.payload[i]);
        }
        value[0].forEach((item) => {
          obj.time.push(item.countDate);
          obj.num1.push(item.incrementNum);
        });
        value[1].forEach((item) => {
          obj.num2.push(item.incrementNum);
        });
        writeIn(state, ['sellersIn', 'xAxis', 0, 'data'], obj.time);
        writeIn(state, ['sellersIn', 'series', 0, 'data'], obj.num1);
        writeIn(state, ['sellersIn', 'series', 1, 'data'], obj.num2);
      })
      .addMatcher(api.endpoints.compareSellersOldOrNewInit.matchFulfilled, (state, action) => {
        const obj = {time: [], oldNum1: [], newNum1: [], newRatio1: [], oldNum2: [], newNum2: [], newRatio2: []};
        const value = [];
        let total = 0;
        for (const i in action.payload) {
          value.push(action.payload[i]);
        }
        value[0].forEach((item) => {
          obj.time.push(item.countDate);
          obj.newNum1.push(item.newCustomer);
          obj.oldNum1.push(item.oldCustomer);
          total = parseInt(item.newCustomer) + parseInt(item.oldCustomer);
          if (!total) {
            obj.newRatio1.push(0);
          } else {
            obj.newRatio1.push(parseInt(item.newCustomer / total * 100));
          }
        });
        value[1].forEach((item) => {
          obj.newNum2.push(item.newCustomer);
          obj.oldNum2.push(item.oldCustomer);
          total = parseInt(item.newCustomer) + parseInt(item.oldCustomer);
          if (!total) {
            obj.newRatio2.push(0);
          } else {
            obj.newRatio2.push(parseInt(item.newCustomer / total * 100));
          }
        });
        writeIn(state, ['sellersOldOrNew', 'xAxis', 0, 'data'], obj.time);
        writeIn(state, ['sellersOldOrNew', 'series', 0, 'data'], obj.newRatio1);
        writeIn(state, ['sellersOldOrNew', 'series', 1, 'data'], obj.newRatio2);
        writeIn(state, ['sellersOldOrNew', 'xAxis', 0, 'newNum1'], obj.newNum1);
        writeIn(state, ['sellersOldOrNew', 'xAxis', 0, 'newNum2'], obj.newNum2);
        writeIn(state, ['sellersOldOrNew', 'xAxis', 0, 'oldNum1'], obj.oldNum1);
        writeIn(state, ['sellersOldOrNew', 'xAxis', 0, 'oldNum2'], obj.oldNum2);
      })
      .addMatcher(api.endpoints.compareSellersOutInit.matchFulfilled, (state, action) => {
        const obj = {time: [], jumpNum1: [], jumpNum2: [], jumpRatio1: [], jumpRatio2: []};
        const value = [];
        for (const i in action.payload) {
          value.push(action.payload[i]);
        }
        value[0].forEach((item) => {
          obj.time.push(item.countDate);
          obj.jumpNum1.push(item.jumpNum);
          obj.jumpRatio1.push(item.jumpRatio);
        });
        value[1].forEach((item) => {
          obj.jumpNum2.push(item.jumpNum);
          obj.jumpRatio2.push(item.jumpRatio);
        });
        writeIn(state, ['sellersOut', 'xAxis', 0, 'data'], obj.time);
        writeIn(state, ['sellersOut', 'series', 0, 'data'], obj.jumpRatio1);
        writeIn(state, ['sellersOut', 'series', 1, 'data'], obj.jumpRatio2);
        writeIn(state, ['sellersOut', 'xAxis', 0, 'jumpNum1'], obj.jumpNum1);
        writeIn(state, ['sellersOut', 'xAxis', 0, 'jumpNum2'], obj.jumpNum2);
      })
      .addMatcher(api.endpoints.compareSellersDeepInit.matchFulfilled, (state, action) => {
        const obj = {time: []/* ,jumpNum:[],jumpRatio:[]*/, deepNum1: [], deepNum2: [], deepRatio1: [], deepRatio2: []};
        const value = [];
        for (const i in action.payload) {
          value.push(action.payload[i]);
        }
        value[0].forEach((item) => {
          obj.time.push(item.countDate);
          obj.deepNum1.push(item.deepNum);
          obj.deepRatio1.push(item.deepRatio);
        });
        value[1].forEach((item) => {
          obj.deepNum2.push(item.deepNum);
          obj.deepRatio2.push(item.deepRatio);
        });
        writeIn(state, ['sellersDeep', 'xAxis', 0, 'data'], obj.time);
        writeIn(state, ['sellersDeep', 'series', 0, 'data'], obj.deepRatio1);
        writeIn(state, ['sellersDeep', 'series', 1, 'data'], obj.deepRatio2);
        writeIn(state, ['sellersDeep', 'xAxis', 0, 'deepNum1'], obj.deepNum1);
        writeIn(state, ['sellersDeep', 'xAxis', 0, 'deepNum2'], obj.deepNum2);
      })
      .addMatcher(api.endpoints.compareSellersStayInit.matchFulfilled, (state, action) => {
        const obj = {key: [], value1: [], value2: []};
        const value = [];
        for (const i in action.payload) {
          value.push(action.payload[i]);
        }
        value[0].forEach((item) => {
          obj.key.push(item.key);
          obj.value1.push(item.value);
        });
        value[1].forEach((item) => {
          obj.value2.push(item.value);
        });
        writeIn(state, ['sellersStay', 'xAxis', 0, 'data'], obj.key);
        writeIn(state, ['sellersStay', 'series', 0, 'data'], obj.value1);
        writeIn(state, ['sellersStay', 'series', 1, 'data'], obj.value2);
      })
      .addMatcher(api.endpoints.compareSellersActiveInit.matchFulfilled, (state, action) => {
        const obj = {key: [], value1: [], value2: []};
        const value = [];
        for (const i in action.payload) {
          value.push(action.payload[i]);
        }
        value[0].forEach((item) => {
          obj.key.push(item.key);
          obj.value1.push(item.value);
        });
        value[1].forEach((item) => {
          obj.value2.push(item.value);
        });
        writeIn(state, ['sellersActive', 'xAxis', 0, 'data'], obj.key);
        writeIn(state, ['sellersActive', 'series', 0, 'data'], obj.value1);
        writeIn(state, ['sellersActive', 'series', 1, 'data'], obj.value2);
      })
      .addMatcher(api.endpoints.compareSellersTimeSectionInit.matchFulfilled, (state, action) => {
        const obj = {time: [], num1: [], num2: []};
        const value = [];
        for (const i in action.payload) {
          value.push(action.payload[i]);
        }
        value[0].forEach((item) => {
          obj.time.push(item.key);
          obj.num1.push(item.value);
        });
        value[1].forEach((item) => {
          obj.num2.push(item.value);
        });
        writeIn(state, ['sellersTimeSection', 'xAxis', 0, 'data'], obj.time);
        writeIn(state, ['sellersTimeSection', 'series', 0, 'data'], obj.num1);
        writeIn(state, ['sellersTimeSection', 'series', 1, 'data'], obj.num2);
      })
      .addMatcher(api.endpoints.compareSellersCycleInit.matchFulfilled, (state, action) => {
        const obj = {key: [], value1: [], value2: []};
        const value = [];
        for (const i in action.payload) {
          value.push(action.payload[i]);
        }
        value[0].forEach((item) => {
          obj.key.push(item.key);
          obj.value1.push(item.value);
        });
        value[1].forEach((item) => {
          obj.value2.push(item.value);
        });
        writeIn(state, ['sellersCycle', 'xAxis', 0, 'data'], obj.key);
        writeIn(state, ['sellersCycle', 'series', 0, 'data'], obj.value1);
        writeIn(state, ['sellersCycle', 'series', 1, 'data'], obj.value2);
      });
  }
});

export default compareSlice.reducer;
