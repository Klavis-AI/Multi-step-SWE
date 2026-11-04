import {createApi, fetchBaseQuery} from '@reduxjs/toolkit/query/react';

import actionApi from '../constants/actionApi';

const firstPageUrl = actionApi.firstPage;
const sellersUrl = actionApi.sellers;
const statisticsUrl = actionApi.statistics;
const compareUrl = actionApi.compare;

/* One endpoint per request the screens used to make, named after the thunk it
   replaces so the mapping stays readable.  Two properties of the old data layer
   are load-bearing and are restored here rather than left to the defaults:

   `refetchOnMountOrArgChange: true` - every screen used to fire its request
   from componentDidMount, so arriving at a screen a second time asked the
   server again.  Without this a cached entry is served and the chart never
   receives a fresh response.

   An endpoint per *call site* rather than per URL.  Several of these paths are
   the same string: statisticsOut and statisticsDeep, compare's out and
   deepVisit, compare's sellersOut and sellersDeep.  They are separate endpoints
   because they are separate charts, mounted together on one page, and each one
   asks for its own copy - which is exactly what the four screens did before. */
const api = createApi({
  reducerPath: 'api',
  baseQuery: fetchBaseQuery({baseUrl: actionApi.baseUrl}),
  refetchOnMountOrArgChange: true,
  endpoints: builder => ({
    // ---- 首页 ----------------------------------------------------------
    firstPageNumInit: builder.query({
      query: () => `${firstPageUrl.numInit}/`
    }),
    firstPageFetch: builder.query({
      query: () => `${firstPageUrl.fetch}/`
    }),
    firstPageSellersInit: builder.query({
      query: () => `${firstPageUrl.sellersInit}/`
    }),

    // ---- 商家 ----------------------------------------------------------
    sellersList: builder.query({
      query: () => `${sellersUrl.sellersListInit}/`
    }),
    allSellersLineChart: builder.query({
      query: ({time, chartPage}) => `${sellersUrl.allSellersLineChartInit + time}/${chartPage}/`
    }),
    singleSellerCustomerNumInit: builder.query({
      query: name => `${sellersUrl.singleSellerCustomerNumInit + name}/`
    }),
    singleSellerCustomerNumFetch: builder.query({
      query: id => `${sellersUrl.singleSellerCustomerNumFetch + id}/`
    }),
    singleSellerCustomerAvgInit: builder.query({
      query: ({id, param}) => `${sellersUrl.singleSellerCustomerAvgInit + id}/${param}/`
    }),
    singleSellerCustomerFlowInit: builder.query({
      query: ({id, param}) => `${sellersUrl.singleSellerCustomerFlowInit + id}/${param}/`
    }),
    singleSellerCustomerInInit: builder.query({
      query: ({id, param}) => `${sellersUrl.singleSellerCustomerInInit + id}/${param}/`
    }),
    singleSellerRadar: builder.query({
      query: id => `${sellersUrl.singleSellerRadar + id}/`
    }),
    singleSellerStayBar: builder.query({
      query: ({id, param}) => `${sellersUrl.singleSellerStayBar + id}/${param}/`
    }),
    singleSellerOldOrNew: builder.query({
      query: ({id, param}) => `${sellersUrl.singleSellerOldOrNew + id}/${param}/`
    }),
    singleSellerTimeSection: builder.query({
      query: ({id, param}) => `${sellersUrl.singleSellerTimeSection + id}/${param}/`
    }),
    singleSellerDeepVisit: builder.query({
      query: ({id, param}) => `${sellersUrl.singleSellerDeepVisit + id}/${param}/`
    }),
    singleSellerCycleAndActive: builder.query({
      query: ({id, param}) => `${sellersUrl.singleSellerCycleAndActive + id}/${param}/`
    }),
    singleSellerCycleInit: builder.query({
      query: ({id, param}) => `${sellersUrl.singleSellerCycleInit + id}/${param}/`
    }),
    singleSellerActiveInit: builder.query({
      query: ({id, param}) => `${sellersUrl.singleSellerActiveInit + id}/${param}/`
    }),

    // ---- 统计 ----------------------------------------------------------
    statisticsCustomerNumInit: builder.query({
      query: time => `${statisticsUrl.statisticsCustomerNumInit + time}/`
    }),
    statisticsAvgInit: builder.query({
      query: time => `${statisticsUrl.statisticsAvgInit + time}/`
    }),
    statisticsPeakInit: builder.query({
      query: time => `${statisticsUrl.statisticsPeakInit + time}/`
    }),
    statisticsDeepInit: builder.query({
      query: time => `${statisticsUrl.statisticsDeepInit + time}/`
    }),
    statisticsOutInit: builder.query({
      query: time => `${statisticsUrl.statisticsOutInit + time}/`
    }),
    statisticsOldOrNewInit: builder.query({
      query: time => `${statisticsUrl.statisticsOldOrNewInit + time}/`
    }),
    statisticsTimeSectionInit: builder.query({
      query: time => `${statisticsUrl.statisticsTimeSectionInit + time}/`
    }),
    statisticsCycleInit: builder.query({
      query: time => `${statisticsUrl.statisticsCycleInit + time}/`
    }),
    statisticsActiveInit: builder.query({
      query: time => `${statisticsUrl.statisticsActiveInit + time}/`
    }),
    statisticsStayInit: builder.query({
      query: time => `${statisticsUrl.statisticsStayInit + time}/`
    }),

    // ---- 对比：整体 ----------------------------------------------------
    compareNumInit: builder.query({
      query: ({time, range}) => `${compareUrl.numInit + time}/${range}/`
    }),
    compareNumInInit: builder.query({
      query: ({time, range}) => `${compareUrl.numInInit + time}/${range}/`
    }),
    compareNumAvgInit: builder.query({
      query: ({time, range}) => `${compareUrl.numAvgInit + time}/${range}/`
    }),
    compareOldOrNewInit: builder.query({
      query: ({time, range}) => `${compareUrl.oldOrNewInit + time}/${range}/`
    }),
    compareDeepVisitInit: builder.query({
      query: ({time, range}) => `${compareUrl.deepVisitInit + time}/${range}/`
    }),
    compareOutInit: builder.query({
      query: ({time, range}) => `${compareUrl.outInit + time}/${range}/`
    }),
    compareActiveInit: builder.query({
      query: ({time, range}) => `${compareUrl.activeInit + time}/${range}/`
    }),
    compareCycleInit: builder.query({
      query: ({time, range}) => `${compareUrl.cycleInit + time}/${range}/`
    }),
    compareStayInit: builder.query({
      query: ({time, range}) => `${compareUrl.stayInit + time}/${range}/`
    }),

    // ---- 对比：商家 ----------------------------------------------------
    compareSellersList: builder.query({
      query: () => `${compareUrl.sellersListInit}/`
    }),
    compareSellersNumInit: builder.query({
      query: sellersAndTime => `${compareUrl.sellersNumInit + sellersAndTime}/`
    }),
    compareSellersAvgInit: builder.query({
      query: sellersAndTime => `${compareUrl.sellersAvgInit + sellersAndTime}/`
    }),
    compareSellersInInit: builder.query({
      query: sellersAndTime => `${compareUrl.sellersInInit + sellersAndTime}/`
    }),
    compareSellersOldOrNewInit: builder.query({
      query: sellersAndTime => `${compareUrl.sellersOldOrNewInit + sellersAndTime}/`
    }),
    compareSellersOutInit: builder.query({
      query: sellersAndTime => `${compareUrl.sellersOutInit + sellersAndTime}/`
    }),
    compareSellersDeepInit: builder.query({
      query: sellersAndTime => `${compareUrl.sellersDeepInit + sellersAndTime}/`
    }),
    compareSellersStayInit: builder.query({
      query: sellersAndTime => `${compareUrl.sellersStayInit + sellersAndTime}/`
    }),
    compareSellersActiveInit: builder.query({
      query: sellersAndTime => `${compareUrl.sellersActiveInit + sellersAndTime}/`
    }),
    compareSellersTimeSectionInit: builder.query({
      query: sellersAndTime => `${compareUrl.sellersTimeSectionInit}?${sellersAndTime}/`
    }),
    compareSellersCycleInit: builder.query({
      query: sellersAndTime => `${compareUrl.sellersCycleInit + sellersAndTime}/`
    })
  })
});

export const {
  useFirstPageNumInitQuery,
  useLazyFirstPageFetchQuery,
  useFirstPageSellersInitQuery,
  useLazyFirstPageSellersInitQuery,

  useSellersListQuery,
  useLazyAllSellersLineChartQuery,
  useLazySingleSellerCustomerNumInitQuery,
  useLazySingleSellerCustomerNumFetchQuery,
  useSingleSellerCustomerAvgInitQuery,
  useSingleSellerCustomerFlowInitQuery,
  useSingleSellerCustomerInInitQuery,
  useSingleSellerRadarQuery,
  useSingleSellerStayBarQuery,
  useSingleSellerOldOrNewQuery,
  useSingleSellerTimeSectionQuery,
  useSingleSellerDeepVisitQuery,
  useSingleSellerCycleAndActiveQuery,
  useSingleSellerCycleInitQuery,
  useSingleSellerActiveInitQuery,

  useStatisticsCustomerNumInitQuery,
  useStatisticsAvgInitQuery,
  useStatisticsPeakInitQuery,
  useStatisticsDeepInitQuery,
  useStatisticsOutInitQuery,
  useStatisticsOldOrNewInitQuery,
  useStatisticsTimeSectionInitQuery,
  useStatisticsCycleInitQuery,
  useStatisticsActiveInitQuery,
  useStatisticsStayInitQuery,

  useLazyCompareNumInitQuery,
  useLazyCompareNumInInitQuery,
  useLazyCompareNumAvgInitQuery,
  useLazyCompareOldOrNewInitQuery,
  useLazyCompareDeepVisitInitQuery,
  useLazyCompareOutInitQuery,
  useLazyCompareActiveInitQuery,
  useLazyCompareCycleInitQuery,
  useLazyCompareStayInitQuery,

  useCompareSellersListQuery,
  useLazyCompareSellersNumInitQuery,
  useLazyCompareSellersAvgInitQuery,
  useLazyCompareSellersInInitQuery,
  useLazyCompareSellersOldOrNewInitQuery,
  useLazyCompareSellersOutInitQuery,
  useLazyCompareSellersDeepInitQuery,
  useLazyCompareSellersStayInitQuery,
  useLazyCompareSellersActiveInitQuery,
  useLazyCompareSellersTimeSectionInitQuery,
  useLazyCompareSellersCycleInitQuery
} = api;

export default api;
