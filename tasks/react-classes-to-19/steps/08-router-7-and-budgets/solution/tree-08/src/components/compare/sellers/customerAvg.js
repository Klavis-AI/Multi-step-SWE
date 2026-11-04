import React, {useEffect, useRef} from 'react';
import {useSelector} from 'react-redux';
import { useOutletContext } from 'react-router';
import echarts from 'echarts/lib/echarts';
import Calendar from '../../calendar';
// import '../../styles/calendar.scss';
import 'echarts/lib/chart/line';
import 'echarts/lib/chart/bar';
import 'echarts/lib/component/title';
import 'echarts/lib/component/legend';
import 'echarts/lib/component/tooltip';
import 'echarts/lib/component/grid';
import 'echarts/lib/component/toolbox';
import { FaQuestion } from 'react-icons/fa';

import Tools from '../../tools';
import {useLazyCompareSellersAvgInitQuery} from '../../../store/api';

function SellersAvg() {
  const {sellersAndTime} = useOutletContext();
  const sellersAvg = useSelector(state => state.compare.sellersAvg);
  // Both fetches sit inside effects that are doing other imperative work: the
  // mount effect only fetches once it has two 商家 to compare, and the
  // componentWillReceiveProps effect fetches on the round that brings new ones
  // and then returns.  A lazy query is triggered where the thunk was dispatched.
  const [loadAvg] = useLazyCompareSellersAvgInitQuery();

  /* The table's values were 5 useState that the change effect assigned on
     every round that carried data, which is a render's worth of work done a
     render late: each one is a read of the response the store already holds.
     Render derives them the way it derives the two names.  The slice only ever
     writes sellersAvg on a fulfilled response and never clears it, so the effect's
     "only when there is data" guard and this fallback hold at exactly the same
     times. */
  const hasData = sellersAvg.series[0].data && sellersAvg.series[0].data[0];
  const timeList = hasData ? sellersAvg.xAxis[0].data : '';
  const num1List = hasData ? sellersAvg.series[0].data : '';
  const num2List = hasData ? sellersAvg.series[1].data : '';
  const percent1List = hasData ? sellersAvg.xAxis[0].percent1 : '';
  const percent2List = hasData ? sellersAvg.xAxis[0].percent2 : '';

  /* Both names are sellersAndTime cut in two, so render derives them.  They
     used to live in a mutable object in a ref that the two effects wrote and
     render read out of `.current`, which is the read the compiler refuses to
     compile - and the ref was never carrying anything the outlet context did
     not already have.  Deriving them also retires the forced render the change
     effect asked for: the round that brings new 商家 renders with the new
     names in it. */
  const [seller1 = '', seller2 = ''] =
    sellersAndTime.length > 1 ? sellersAndTime.split('/')[0].split(',') : [];

  const chartEl = useRef(null);
  const compareSellerAvgChart = useRef('');
  const resizeHandler = useRef(null);

  /* componentWillMount had a body of one commented-out log and nothing else.
     There is no effect to write for a lifecycle that did nothing. */
        // console.log('componentWillMount-- ')

  /* What is left of componentDidMount once the fetch and the chart's own
     creation have gone to the effect below: the resize listener, and the
     teardown that componentWillUnmount did.  It reads nothing but refs, so the
     empty dependency list is the honest one rather than a suppressed one. */
  useEffect(() => {
    const resizeFun = () => {
      if (resizeHandler.current) {
        clearTimeout(resizeHandler.current);
      }
      if (compareSellerAvgChart.current) {
        resizeHandler.current = setTimeout(() => {
          compareSellerAvgChart.current.resize();
        }, 100);
      }
    };

    window.addEventListener('resize', resizeFun);
    return () => {
        // console.log('1componentWillUnmount');
      // The chart is only initialised once there are two 商家 to compare, so this
      // cleanup can run with nothing to dispose - which is what StrictMode does
      // immediately after mounting, and what leaving the screen without choosing
      // a pair always did.
      if (compareSellerAvgChart.current) {
        compareSellerAvgChart.current.dispose();
      }
      window.removeEventListener('resize', resizeFun);
    };
  }, []);

  /* componentDidMount's fetch and componentWillReceiveProps, which were two
     effects with two dependency lists and the same subject: the chart, and
     which response belongs in it.  Keeping them apart is what forced the mount
     list to be empty while its body read sellersAndTime and the query trigger.
     Together they take one honest list, and the `return` that was part of the
     lifecycle still is: on the round that brings new 商家 the chart is torn
     down and re-initialised and the data branch is deliberately not reached,
     so the loading state stands until that pair's own response lands.  The
     previous sellersAndTime is held in a ref because the comparison the
     lifecycle was handed has no other way into an effect; an unmade chart is
     what the mount round is, and it takes the same branch. */
  const prevSellersAndTime = useRef(sellersAndTime);
  useEffect(() => {
        // 有参数传入，才发送请求渲染图表。防止无限循环发送请求，要两次props对比，不同才发
    if (sellersAndTime !== prevSellersAndTime.current || !compareSellerAvgChart.current) {
      prevSellersAndTime.current = sellersAndTime;
      // 进入此路由时，已经选择了时间参数，可以立即渲染图表
      if (sellersAndTime.length > 1) {
        loadAvg(sellersAndTime);
        compareSellerAvgChart.current = echarts.init(chartEl.current);
        compareSellerAvgChart.current.showLoading();
      }
      return;
    }
    const next = Tools.copyOption(sellersAvg);
    if (next.series[0].data && next.series[0].data[0]) {
      next.legend.data.push(seller1, seller2);
      next.series[0].name = seller1;
      next.series[1].name = seller2;
      compareSellerAvgChart.current.setOption(next);
      compareSellerAvgChart.current.hideLoading();
    }
  }, [sellersAndTime, sellersAvg, loadAvg, seller1, seller2]);

  const rows = [];
  if (timeList) {
    timeList.forEach((item, i) => {
      rows.push(<tr key={i}><td>{timeList[i]}</td><td>{num1List[i]}</td><td>{percent1List[i]}%</td><td>{num2List[i]}</td><td>{percent2List[i]}%</td></tr>);
    });
  }

  return (<div>
    <div className="panel">
      <div className="panelHead">客流量对比&nbsp;<FaQuestion className="questionMark" />
        <div className="messageMark"><p>展示两个商家在同一时间段内的客流平均值对比<br /></p></div></div>
      <div className="panelBody">
        <div ref={chartEl} className="compareSellerAvgChart" />
      </div>
    </div>
    <div className="panel">
      <div className="panelHead">客流量对比明细</div>
      <div className="panelBody">
        <table className="Table">
          <thead>
            <tr><th>时间</th><th>{seller1}人数</th><th>总体占比</th><th>{seller2}人数</th><th>总体占比</th></tr>
          </thead>
          <tbody>
            {rows}
          </tbody>
        </table>
      </div>
    </div>
  </div>);
}

export default SellersAvg;
