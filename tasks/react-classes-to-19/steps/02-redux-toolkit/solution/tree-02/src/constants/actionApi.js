/* The URL table, unchanged in content.  Every entry is now the path relative to
   `baseUrl` rather than an absolute URL, because the query functions in
   `store/api.js` hand their result to fetchBaseQuery, which joins it onto the
   base itself.  The duplicates below are the server's, not a transcription
   slip: statisticsOut and statisticsDeep are served by one route, as are
   compare's out/deepVisit and sellersOut/sellersDeep. */
const baseUrl = 'http://localhost:3000/';

export default {
  baseUrl,
  firstPage: {
    numInit: 'hour/init',
    fetch: 'hour/fetch',
    sellersInit: 'index/sellersInit'
  },
  sellers: {
    sellersListInit: 'userList',
    allSellersLineChartInit: 'index/allsellers/',
    // allSellersTableInit:'http://localhost:3003/allsellers/allSellersTableInit',
    singleSellerLineChartInit: 'hour/singleSeller/',
    singleSellerCustomerNumInit: 'hour/singleSellerCustomerNumInit/',
    singleSellerCustomerNumFetch: 'hour/singleSellerCustomerNumFetch/',
    singleSellerCustomerInInit: 'hour/singleSellerCustomerInInit/',
    singleSellerCustomerAvgInit: 'hour/singleSellerCustomerAvgInit/',
    singleSellerCustomerFlowInit: 'hour/singleSellerCustomerFlowInit/',
    singleSellerRadar: 'hour/singleSellerRadar/',
    singleSellerStayBar: 'hour/singleSellerStayBar/',
    singleSellerOldOrNew: 'hour/singleSellerOldOrNew/',
    singleSellerTimeSection: 'hour/singleSellerTimeSection/',
    singleSellerDeepVisit: 'hour/singleSellerDeepVisit/',
    singleSellerCycleAndActive: 'hour/singleSellerCycleAndActive/',
    singleSellerCycleInit: 'index/singleSellerCycleInit/',
    singleSellerActiveInit: 'index/singleSellerActiveInit/'
  },
  statistics: {
    statisticsCustomerNumInit: 'index/statisticsCustomerNumInit/',
    statisticsAvgInit: 'index/statisticsAvgInit/',
    statisticsPeakInit: 'index/statisticsPeakInit/',
    statisticsDeepInit: 'index/statisticsDeepInit/',
    statisticsOutInit: 'index/statisticsDeepInit/',
    statisticsOldOrNewInit: 'hour/statisticsOldOrNewInit/',
    statisticsTimeSectionInit: 'hour/statisticsTimeSectionInit/',
    statisticsCycleInit: 'index/statisticsCycleInit/',
    statisticsActiveInit: 'index/statisticsActiveInit/',
    statisticsStayInit: 'index/statisticsStayInit/'
  },
  compare: {
    numInit: 'index/compareCustomerNum/',
    numInInit: 'index/numInInit/',
    numAvgInit: 'index/numAvgInit/',
    cycleInit: 'index/customerCycleInit/',
    activeInit: 'index/customerActiveInit/',
    oldOrNewInit: 'index/customerOldOrNewInit/',
    deepVisitInit: 'index/customerDeepVisitInit/',
    stayInit: 'index/customerStayInit/',
    outInit: 'index/customerDeepVisitInit/',
    sellersAvgInit: 'index/sellersAvgInit/',
    sellersNumInit: 'index/sellersNumInit/',
    sellersListInit: 'userList',
    sellersInInit: 'index/sellersInInit/',
    sellersOldOrNewInit: 'index/sellersOldOrNewInit/',
    sellersOutInit: 'index/sellersDeepInit/',
    sellersDeepInit: 'index/sellersDeepInit/',
    sellersStayInit: 'index/sellersStayBarInit/',
    sellersActiveInit: 'index/sellersActiveInit/',
    sellersTimeSectionInit: 'index/sellersTimeSectionInit',
    sellersCycleInit: 'index/sellersCycleInit/'
  }
};
