
import React from 'react';
import ReactDOM from 'react-dom';
import {Provider} from 'react-redux';
import { Router, Route, IndexRoute, hashHistory, Redirect, IndexRedirect} from 'react-router';
import General from './general';
// import Sellers from './sellers';
import FirstPage from './firstPage/firstPage';
import store from '../store/store';
import FirstPageChart from './firstPage/firstPageChart';
import FirstPageSellers from './firstPage/allSellers';
import Carousel from './firstPage/Carousel';


import Calendar from './calendar';
import NotFindPage from './notFindPage';

/* ---------singleSeller开始*/
// require.ensure is a webpack-2 extension; the same code split is expressed as a
// dynamic import(), which react-router 3's getComponent takes the same way.
const Sellers = (location, callback) => {
  import('./sellers/sellers').then((m) => callback(null, m.default));
};

const Allsellers = (location, callback) => {
  import('./sellers/allSellers').then((m) => callback(null, m.default));
};
const SingleSellerRoute = (location, callback) => {
  import('./sellers/singleSellerRoute').then((m) => callback(null, m.default));
};
const SingleSellerChartCurrent = (location, callback) => {
  import('./sellers/singleSellerChartCurrent').then((m) => callback(null, m.default));
};
const SingleSellerChartHistory = (location, callback) => {
  import('./sellers/singleSellerChartHistory').then((m) => callback(null, m.default));
};
const CustomerIn = (location, callback) => {
  import('./sellers/singleSellerChartHistory/customerIn').then((m) => callback(null, m.default));
};
const CustomerFlow = (location, callback) => {
  import('./sellers/singleSellerChartHistory/customerFlow').then((m) => callback(null, m.default));
};
const CustomerAvg = (location, callback) => {
  import('./sellers/singleSellerChartHistory/customerAvg').then((m) => callback(null, m.default));
};
const Radar = (location, callback) => {
  import('./sellers/singleSellerChartHistory/radar').then((m) => callback(null, m.default));
};
const StayBar = (location, callback) => {
  import('./sellers/singleSellerChartHistory/stayBar').then((m) => callback(null, m.default));
};
const OldOrNew = (location, callback) => {
  import('./sellers/singleSellerChartHistory/oldOrNew').then((m) => callback(null, m.default));
};
const TimeSection = (location, callback) => {
  import('./sellers/singleSellerChartHistory/timeSection').then((m) => callback(null, m.default));
};
const DeepVisit = (location, callback) => {
  import('./sellers/singleSellerChartHistory/deepVisit').then((m) => callback(null, m.default));
};
const Cycle = (location, callback) => {
  import('./sellers/singleSellerChartHistory/cycle').then((m) => callback(null, m.default));
};
const Active = (location, callback) => {
  import('./sellers/singleSellerChartHistory/active').then((m) => callback(null, m.default));
};
// import Allsellers from './allSellers';
// import SingleSellerRoute from './singleSellerRoute';
// import SingleSellerChartCurrent from './singleSellerChartCurrent';
// import SingleSellerChartHistory from './singleSellerChartHistory';
// import CustomerIn from './sellers/singleSellerChartHistory/customerIn';
// import CustomerFlow from './sellers/singleSellerChartHistory/customerFlow';
// import Radar from './sellers/singleSellerChartHistory/radar';
// import StayBar from './sellers/singleSellerChartHistory/stayBar';
// import OldOrNew from './sellers/singleSellerChartHistory/oldOrNew';
// import TimeSection from './sellers/singleSellerChartHistory/timeSection';
// import DeepVisit from './sellers/singleSellerChartHistory/deepVisit';
// import Cycle from './sellers/singleSellerChartHistory/cycle';
// import Active from './sellers/singleSellerChartHistory/active';

/* ---------singleSeller结束*/

/* ---------statistics开始*/
const Statistics = (location, callback) => {
  import('./statistics/statistics').then((m) => callback(null, m.default));
};
const StatCustomerNum = (location, callback) => {
  import('./statistics/customerNum').then((m) => callback(null, m.default));
};
const StatCustomerPeak = (location, callback) => {
  import('./statistics/customerPeak').then((m) => callback(null, m.default));
};
const StatCustomerAvg = (location, callback) => {
  import('./statistics/customerAvg').then((m) => callback(null, m.default));
};
const StatOldOrNew = (location, callback) => {
  import('./statistics/oldOrNew').then((m) => callback(null, m.default));
};
const StatDeep = (location, callback) => {
  import('./statistics/deep').then((m) => callback(null, m.default));
};
const StatOut = (location, callback) => {
  import('./statistics/out').then((m) => callback(null, m.default));
};
const StatStay = (location, callback) => {
  import('./statistics/stay').then((m) => callback(null, m.default));
};
const StatTimeSection = (location, callback) => {
  import('./statistics/timeSection').then((m) => callback(null, m.default));
};
const StatCycle = (location, callback) => {
  import('./statistics/cycle').then((m) => callback(null, m.default));
};
const StatActive = (location, callback) => {
  import('./statistics/active').then((m) => callback(null, m.default));
};
// import Statistics from './statistics/statistics';
// import StatCustomerNum from './statistics/customerNum';
// import StatCustomerPeak from './statistics/customerPeak';
// import StatOldOrNew from './statistics/oldOrNew';
// import StatTimeSection from './statistics/timeSection';
// import StatCycle from './statistics/cycle';
// import StatActive from './statistics/active';
/* ---------statistics结束*/

/* ---------compare开始*/
const Compare = (location, callback) => {
  import('./compare/compare').then((m) => callback(null, m.default));
};
const WholeCustomerAvg = (location, callback) => {
  import('./compare/whole/customerAvg').then((m) => callback(null, m.default));
};
const WholeCustomerNum = (location, callback) => {
  import('./compare/whole/customerNum').then((m) => callback(null, m.default));
};
const WholeCustomerIn = (location, callback) => {
  import('./compare/whole/customerIn').then((m) => callback(null, m.default));
};
const WholeActive = (location, callback) => {
  import('./compare/whole/active').then((m) => callback(null, m.default));
};
const WholeCycle = (location, callback) => {
  import('./compare/whole/cycle').then((m) => callback(null, m.default));
};
const WholeOldOrNew = (location, callback) => {
  import('./compare/whole/oldOrNew').then((m) => callback(null, m.default));
};
const WholeStay = (location, callback) => {
  import('./compare/whole/stay').then((m) => callback(null, m.default));
};
const WholeDeepVisit = (location, callback) => {
  import('./compare/whole/deepVisit').then((m) => callback(null, m.default));
};
const WholeOut = (location, callback) => {
  import('./compare/whole/out').then((m) => callback(null, m.default));
};

const SellersRoute = (location, callback) => {
  import('./compare/sellers/sellersRoute').then((m) => callback(null, m.default));
};
// const SellersCustomerIn = (location, callback) => {
//     require.ensure([], require => {
//         callback(null, require('./compare/sellers/customerNum').default)
//     },'sellerCustomerIn')
// }
// const SellersCustomerNum = (location, callback) => {
//     require.ensure([], require => {
//         callback(null, require('./compare/sellers/customerIn').default)
//     },'sellerOldOrNew')
// }
// const SellersCustomerNum = (location, callback) => {
//     require.ensure([], require => {
//         callback(null, require('./compare/sellers/customerIn').default)
//     },'sellerDeep')
// }
// import Compare from './compare/compare';
// import WholeCustomerNum from './compare/whole/customerNum';
// import WholeCustomerIn from './compare/whole/customerIn';
// import WholeActive from './compare/whole/active';
// import WholeCycle from './compare/whole/cycle';
/* import WholeDeepVisit from './compare/whole/deepVisit';
   import WholeOut from './compare/whole/out';*/
// import WholeOldOrNew from './compare/whole/oldOrNew';
// import SellersRoute from './compare/sellers/sellersRoute';
import SellersCustomerAvg from './compare/sellers/customerAvg';
import SellersCustomerNum from './compare/sellers/customerNum';
import SellersCustomerIn from './compare/sellers/customerIn';
import SellersOldOrNew from './compare/sellers/oldOrNew';
import SellersOut from './compare/sellers/out';
import SellersDeep from './compare/sellers/deep';
import SellersStay from './compare/sellers/stay';
import SellersActive from './compare/sellers/active';
import SellersTimeSection from './compare/sellers/timeSection';
import SellersCycle from './compare/sellers/cycle';

/* ---------compare结束*/


ReactDOM.render(
  <Provider store={store} >
    <Router history={hashHistory}>

      <Route path="/" component={General}>
        <IndexRedirect to="/firstPage/total" />
        <Redirect from="firstPage" to="/firstPage/total" />
        <Route path="firstPage" component={FirstPage}>
          <IndexRoute component={Carousel} />
          <Route path="total" component={Carousel} />
          <Route path="allSellers" component={FirstPageSellers} />
          {/* <Route path="firstFloor" component={FirstFloorChart}/>
          <Route path="secondFloor" component={SecondFloorChart}/>
          <Route path="thirdFloor" component={ThirdFloorChart}/>*/}
        </Route>
        <Redirect from="sellers" to="/sellers/allsellers" />
        <Route path="sellers" getComponent={Sellers}>
          <IndexRoute getComponent={Allsellers} />
          <Route path="allsellers" getComponent={Allsellers} />
          <Redirect from=":id" to=":id/now" />
          <Route path=":id" getComponent={SingleSellerRoute}>
            <IndexRoute getComponent={SingleSellerChartCurrent} />
            <Route path="now" getComponent={SingleSellerChartCurrent} />
            <Redirect from="history" to="history/customerAvg" />
            <Route path="history" getComponent={SingleSellerChartHistory}>
              <IndexRoute getComponent={CustomerAvg} />
              <Route path="customerIn" getComponent={CustomerIn} />
              <Route path="customerAvg" getComponent={CustomerAvg} />
              <Route path="customerFlow" getComponent={CustomerFlow} />
              <Route path="radar" getComponent={Radar} />
              <Route path="stayBar" getComponent={StayBar} />
              <Route path="OldOrNew" getComponent={OldOrNew} />
              <Route path="timeSection" getComponent={TimeSection} />
              <Route path="deepVisit" getComponent={DeepVisit} />
              <Route path="cycle" getComponent={Cycle} />
              <Route path="active" getComponent={Active} />
            </Route>
          </Route>
        </Route>
        <Redirect from="statistics" to="/statistics/index" />
        <Route path="statistics" getComponent={Statistics}>
          <IndexRoute component={FirstPageChart} />
          <Route path="index" component={FirstPageChart} />
          <Route path="total" getComponent={StatCustomerNum} />
          <Route path="peak" getComponent={StatCustomerPeak} />
          <Route path="avg" getComponent={StatCustomerAvg} />
          <Route path="oldOrNew" getComponent={StatOldOrNew} />
          <Route path="deep" getComponent={StatDeep} />
          <Route path="out" getComponent={StatOut} />
          <Route path="stay" getComponent={StatStay} />
          <Route path="timeSection" getComponent={StatTimeSection} />
          <Route path="cycle" getComponent={StatCycle} />
          <Route path="active" getComponent={StatActive} />
        </Route>
        <Redirect from="compare" to="/compare/wholeCustomerAvg" />
        <Route path="compare" getComponent={Compare}>
          <Route path="wholeCustomerAvg" getComponent={WholeCustomerAvg} />
          <Route path="wholeCustomerNum" getComponent={WholeCustomerNum} />
          <Route path="wholeCustomerIn" getComponent={WholeCustomerIn} />
          <Route path="wholeOldOrNew" getComponent={WholeOldOrNew} />
          <Route path="wholeStay" getComponent={WholeStay} />
          <Route path="wholeDeep" getComponent={WholeDeepVisit} />
          <Route path="wholeOut" getComponent={WholeOut} />
          <Route path="wholeCycle" getComponent={WholeCycle} />
          <Route path="wholeActive" getComponent={WholeActive} />
          {/* <Route path='seller*' getComponent={SellersRoute}>
              <Route path='' getComponent={}/>

            </Route>*/}
          <Route path="sellerCustomerAvg" getComponent={SellersRoute}>
            <IndexRoute component={SellersCustomerAvg} />
          </Route>
          <Route path="sellerCustomerNum" getComponent={SellersRoute}>
            <IndexRoute component={SellersCustomerNum} />
          </Route>
          <Route path="sellerCustomerIn" getComponent={SellersRoute}>
            <IndexRoute component={SellersCustomerIn} />
          </Route>
          <Route path="sellerOldOrNew" getComponent={SellersRoute}>
            <IndexRoute component={SellersOldOrNew} />
          </Route>
          <Route path="sellerDeep" getComponent={SellersRoute}>
            <IndexRoute component={SellersDeep} />
          </Route>
          <Route path="sellerOut" getComponent={SellersRoute}>
            <IndexRoute component={SellersOut} />
          </Route>
          <Route path="sellerStay" getComponent={SellersRoute}>
            <IndexRoute component={SellersStay} />
          </Route>
          <Route path="sellerActive" getComponent={SellersRoute}>
            <IndexRoute component={SellersActive} />
          </Route>
          {/* <Route path='sellerTimeSection' getComponent={SellersRoute}>
              <IndexRoute component={SellersTimeSection} />
            </Route>*/}
          <Route path="sellerCycle" getComponent={SellersRoute}>
            <IndexRoute component={SellersCycle} />
          </Route>
        </Route>
        <Route path="*" component={NotFindPage} />
      </Route>
    </Router>
  </Provider>
  , document.getElementById('app')
  );

