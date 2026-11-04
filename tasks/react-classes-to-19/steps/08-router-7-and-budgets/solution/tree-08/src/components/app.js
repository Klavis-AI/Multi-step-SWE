
import React, { lazy, StrictMode } from 'react';
import { createRoot } from 'react-dom/client';
import {Provider} from 'react-redux';
import { createHashRouter, createRoutesFromElements, Navigate, Route, RouterProvider } from 'react-router';
import General from './general';
// import Sellers from './sellers';
import FirstPage from './firstPage/firstPage';
import store from '../store/store';

/* The two first-page panels that draw a chart.  Both are a child route's inline
   element rather than a route with a component of its own, so a route's own
   `lazy` cannot reach them - `lazy` replaces the route's component, and an
   element written inline is not one.  React's `lazy` can: the shell above them
   commits at once and the body arrives under the <Suspense> in
   firstPage/firstPage.js and statistics/statistics.js.  Between them and the ten
   below, this is what takes echarts out of the chunk every visitor downloads. */
const FirstPageChart = lazy(() => import('./firstPage/firstPageChart'));
const FirstPageSellers = lazy(() => import('./firstPage/allSellers'));

/* And the first page's own carousel, which is the one route in the entry that
   the new router's extra weight has to come out of.  It is a leaf route with a
   component of its own, so it takes a route's `lazy` the way the ten below do
   rather than React's; it brings nine react-icons glyphs and the transition
   group with it; and nothing outside `#/firstPage/total` needs any of that. */
const Carousel = () => import('./firstPage/Carousel').then((m) => ({Component: m.default}));


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
/* The nine compare-by-seller panels, and the tenth the table imports and no
   route renders.  Each is the `index` element of a route whose own component is
   SellersRoute, so again the panel is an inline element and only React's `lazy`
   reaches it.  The boundary they arrive under is the one around SellersRoute's
   <Outlet>: the 商家 pickers, the date range and the 查询 button stay on the
   screen while the chart body loads, which is the difference between this and a
   route's `lazy`.

   SellersTimeSection keeps its binding.  Nothing renders it - there is no
   sellerTimeSection route and no link to one - and that is the application as it
   stands; the import is left as the table has it rather than tidied away. */
const SellersCustomerAvg = lazy(() => import('./compare/sellers/customerAvg'));
const SellersCustomerNum = lazy(() => import('./compare/sellers/customerNum'));
const SellersCustomerIn = lazy(() => import('./compare/sellers/customerIn'));
const SellersOldOrNew = lazy(() => import('./compare/sellers/oldOrNew'));
const SellersOut = lazy(() => import('./compare/sellers/out'));
const SellersDeep = lazy(() => import('./compare/sellers/deep'));
const SellersStay = lazy(() => import('./compare/sellers/stay'));
const SellersActive = lazy(() => import('./compare/sellers/active'));
const SellersTimeSection = lazy(() => import('./compare/sellers/timeSection'));
const SellersCycle = lazy(() => import('./compare/sellers/cycle'));

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
      <Route path="total" lazy={Carousel} />
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

// React 18 renders through a root object rather than a call that both creates
// and updates one, and the root is created once for the container the page
// provides.  StrictMode is on around the whole tree: it costs nothing in the
// production build and is what makes the development run report the things
// React 19 will later turn into errors.
createRoot(document.getElementById('app')).render(
  <StrictMode>
    <Provider store={store} >
      <RouterProvider router={router} />
    </Provider>
  </StrictMode>
  );
