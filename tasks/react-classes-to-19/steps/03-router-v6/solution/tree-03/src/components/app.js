
import React from 'react';
import ReactDOM from 'react-dom';
import {Provider} from 'react-redux';
import { createHashRouter, createRoutesFromElements, Navigate, Route, RouterProvider } from 'react-router-dom';
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
// dynamic import(), which a route's own `lazy` takes the same way getComponent
// did: the router awaits it as part of the navigation, so the screen being left
// stays up until the one arriving has loaded.
const Sellers = () => import('./sellers/sellers').then((m) => ({Component: m.default}));

const Allsellers = () => import('./sellers/allSellers').then((m) => ({Component: m.default}));
const SingleSellerRoute = () => import('./sellers/singleSellerRoute').then((m) => ({Component: m.default}));
const SingleSellerChartCurrent = () => import('./sellers/singleSellerChartCurrent').then((m) => ({Component: m.default}));
const SingleSellerChartHistory = () => import('./sellers/singleSellerChartHistory').then((m) => ({Component: m.default}));
const CustomerIn = () => import('./sellers/singleSellerChartHistory/customerIn').then((m) => ({Component: m.default}));
const CustomerFlow = () => import('./sellers/singleSellerChartHistory/customerFlow').then((m) => ({Component: m.default}));
const CustomerAvg = () => import('./sellers/singleSellerChartHistory/customerAvg').then((m) => ({Component: m.default}));
const Radar = () => import('./sellers/singleSellerChartHistory/radar').then((m) => ({Component: m.default}));
const StayBar = () => import('./sellers/singleSellerChartHistory/stayBar').then((m) => ({Component: m.default}));
const OldOrNew = () => import('./sellers/singleSellerChartHistory/oldOrNew').then((m) => ({Component: m.default}));
const TimeSection = () => import('./sellers/singleSellerChartHistory/timeSection').then((m) => ({Component: m.default}));
const DeepVisit = () => import('./sellers/singleSellerChartHistory/deepVisit').then((m) => ({Component: m.default}));
const Cycle = () => import('./sellers/singleSellerChartHistory/cycle').then((m) => ({Component: m.default}));
const Active = () => import('./sellers/singleSellerChartHistory/active').then((m) => ({Component: m.default}));
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
const Statistics = () => import('./statistics/statistics').then((m) => ({Component: m.default}));
const StatCustomerNum = () => import('./statistics/customerNum').then((m) => ({Component: m.default}));
const StatCustomerPeak = () => import('./statistics/customerPeak').then((m) => ({Component: m.default}));
const StatCustomerAvg = () => import('./statistics/customerAvg').then((m) => ({Component: m.default}));
const StatOldOrNew = () => import('./statistics/oldOrNew').then((m) => ({Component: m.default}));
const StatDeep = () => import('./statistics/deep').then((m) => ({Component: m.default}));
const StatOut = () => import('./statistics/out').then((m) => ({Component: m.default}));
const StatStay = () => import('./statistics/stay').then((m) => ({Component: m.default}));
const StatTimeSection = () => import('./statistics/timeSection').then((m) => ({Component: m.default}));
const StatCycle = () => import('./statistics/cycle').then((m) => ({Component: m.default}));
const StatActive = () => import('./statistics/active').then((m) => ({Component: m.default}));
// import Statistics from './statistics/statistics';
// import StatCustomerNum from './statistics/customerNum';
// import StatCustomerPeak from './statistics/customerPeak';
// import StatOldOrNew from './statistics/oldOrNew';
// import StatTimeSection from './statistics/timeSection';
// import StatCycle from './statistics/cycle';
// import StatActive from './statistics/active';
/* ---------statistics结束*/

/* ---------compare开始*/
const Compare = () => import('./compare/compare').then((m) => ({Component: m.default}));
const WholeCustomerAvg = () => import('./compare/whole/customerAvg').then((m) => ({Component: m.default}));
const WholeCustomerNum = () => import('./compare/whole/customerNum').then((m) => ({Component: m.default}));
const WholeCustomerIn = () => import('./compare/whole/customerIn').then((m) => ({Component: m.default}));
const WholeActive = () => import('./compare/whole/active').then((m) => ({Component: m.default}));
const WholeCycle = () => import('./compare/whole/cycle').then((m) => ({Component: m.default}));
const WholeOldOrNew = () => import('./compare/whole/oldOrNew').then((m) => ({Component: m.default}));
const WholeStay = () => import('./compare/whole/stay').then((m) => ({Component: m.default}));
const WholeDeepVisit = () => import('./compare/whole/deepVisit').then((m) => ({Component: m.default}));
const WholeOut = () => import('./compare/whole/out').then((m) => ({Component: m.default}));

const SellersRoute = () => import('./compare/sellers/sellersRoute').then((m) => ({Component: m.default}));
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


// The route table, in the shape react-router 6 takes it: a description of the
// routes, a router created from it, and a provider that renders the match.  The
// addresses are unchanged, and so is the history the application keeps them in -
// this site is served as static files and every address it has ever answered on
// is a hash.
const router = createHashRouter(createRoutesFromElements(
  <Route path="/" element={<General />}>
    <Route index element={<Navigate to="/firstPage/total" replace />} />
    <Route path="firstPage" element={<FirstPage />}>
      <Route index element={<Navigate to="/firstPage/total" replace />} />
      <Route path="total" element={<Carousel />} />
      <Route path="allSellers" element={<FirstPageSellers />} />
    </Route>
    <Route path="sellers" lazy={Sellers}>
      <Route index element={<Navigate to="/sellers/allsellers" replace />} />
      <Route path="allsellers" lazy={Allsellers} />
      <Route path=":id" lazy={SingleSellerRoute}>
        <Route index element={<Navigate to="now" replace />} />
        <Route path="now" lazy={SingleSellerChartCurrent} />
        <Route path="history" lazy={SingleSellerChartHistory}>
          <Route index element={<Navigate to="customerAvg" replace />} />
          <Route path="customerIn" lazy={CustomerIn} />
          <Route path="customerAvg" lazy={CustomerAvg} />
          <Route path="customerFlow" lazy={CustomerFlow} />
          <Route path="radar" lazy={Radar} />
          <Route path="stayBar" lazy={StayBar} />
          <Route path="OldOrNew" lazy={OldOrNew} />
          <Route path="timeSection" lazy={TimeSection} />
          <Route path="deepVisit" lazy={DeepVisit} />
          <Route path="cycle" lazy={Cycle} />
          <Route path="active" lazy={Active} />
        </Route>
      </Route>
    </Route>
    <Route path="statistics" lazy={Statistics}>
      <Route index element={<Navigate to="/statistics/index" replace />} />
      <Route path="index" element={<FirstPageChart />} />
      <Route path="total" lazy={StatCustomerNum} />
      <Route path="peak" lazy={StatCustomerPeak} />
      <Route path="avg" lazy={StatCustomerAvg} />
      <Route path="oldOrNew" lazy={StatOldOrNew} />
      <Route path="deep" lazy={StatDeep} />
      <Route path="out" lazy={StatOut} />
      <Route path="stay" lazy={StatStay} />
      <Route path="timeSection" lazy={StatTimeSection} />
      <Route path="cycle" lazy={StatCycle} />
      <Route path="active" lazy={StatActive} />
    </Route>
    <Route path="compare" lazy={Compare}>
      <Route index element={<Navigate to="/compare/wholeCustomerAvg" replace />} />
      <Route path="wholeCustomerAvg" lazy={WholeCustomerAvg} />
      <Route path="wholeCustomerNum" lazy={WholeCustomerNum} />
      <Route path="wholeCustomerIn" lazy={WholeCustomerIn} />
      <Route path="wholeOldOrNew" lazy={WholeOldOrNew} />
      <Route path="wholeStay" lazy={WholeStay} />
      <Route path="wholeDeep" lazy={WholeDeepVisit} />
      <Route path="wholeOut" lazy={WholeOut} />
      <Route path="wholeCycle" lazy={WholeCycle} />
      <Route path="wholeActive" lazy={WholeActive} />
      <Route path="sellerCustomerAvg" lazy={SellersRoute}>
        <Route index element={<SellersCustomerAvg />} />
      </Route>
      <Route path="sellerCustomerNum" lazy={SellersRoute}>
        <Route index element={<SellersCustomerNum />} />
      </Route>
      <Route path="sellerCustomerIn" lazy={SellersRoute}>
        <Route index element={<SellersCustomerIn />} />
      </Route>
      <Route path="sellerOldOrNew" lazy={SellersRoute}>
        <Route index element={<SellersOldOrNew />} />
      </Route>
      <Route path="sellerDeep" lazy={SellersRoute}>
        <Route index element={<SellersDeep />} />
      </Route>
      <Route path="sellerOut" lazy={SellersRoute}>
        <Route index element={<SellersOut />} />
      </Route>
      <Route path="sellerStay" lazy={SellersRoute}>
        <Route index element={<SellersStay />} />
      </Route>
      <Route path="sellerActive" lazy={SellersRoute}>
        <Route index element={<SellersActive />} />
      </Route>
      <Route path="sellerCycle" lazy={SellersRoute}>
        <Route index element={<SellersCycle />} />
      </Route>
    </Route>
    <Route path="*" element={<NotFindPage />} />
  </Route>,
));

ReactDOM.render(
  <Provider store={store} >
    <RouterProvider router={router} />
  </Provider>
  , document.getElementById('app')
  );
