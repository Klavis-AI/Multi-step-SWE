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
import {useLazyCompareSellersOutInitQuery} from '../../../store/api';

function SellersOut() {
  const {sellersAndTime} = useOutletContext();
  const out = useSelector(state => state.compare.sellersOut);
  // Both fetches sit inside effects that are doing other imperative work: the
  // mount effect only fetches once it has two 商家 to compare, and the
  // componentWillReceiveProps effect fetches on the round that brings new ones
  // and then returns.  A lazy query is triggered where the thunk was dispatched.
  const [loadOut] = useLazyCompareSellersOutInitQuery();

  /* timeList was never in the constructor's state object - only componentWillReceiveProps
     ever put it there - so it starts undefined rather than empty, which is what the
     render guard below is written against. */
  /* The table's values were 5 useState that the change effect assigned on
     every round that carried data, which is a render's worth of work done a
     render late: each one is a read of the response the store already holds.
     Render derives them the way it derives the two names.  The slice only ever
     writes out on a fulfilled response and never clears it, so the effect's
     "only when there is data" guard and this fallback hold at exactly the same
     times. */
  const hasData = out.series[0].data && out.series[0].data[0];
  const timeList = hasData ? out.xAxis[0].data : '';
  const num1List = hasData ? out.xAxis[0].jumpNum1 : '';
  const num2List = hasData ? out.xAxis[0].jumpNum2 : '';
  const percent1List = hasData ? out.series[0].data : '';
  const percent2List = hasData ? out.series[1].data : '';

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
  const compareSellerOutChart = useRef('');
  const resizeHandler = useRef(null);

    // componentWillMount(){
        // console.log('componentWillMount')


    // }
  /* What is left of componentDidMount once the fetch and the chart's own
     creation have gone to the effect below: the resize listener, and the
     teardown that componentWillUnmount did.  It reads nothing but refs, so the
     empty dependency list is the honest one rather than a suppressed one. */
  useEffect(() => {
        // console.log('1componentDidMount');
    //  //dispatch(compareAction.allSellersTableInit());
    		// if(sellersAndTime){
    		// 	compareSellerOutChart.current = echarts.init(chartEl.current);
	     //    compareSellerOutChart.current.showLoading();
    		// }
    const resizeFun = () => {
      if (resizeHandler.current) {
        clearTimeout(resizeHandler.current);
      }
      if (compareSellerOutChart.current) {
        resizeHandler.current = setTimeout(() => {
          compareSellerOutChart.current.resize();
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
      if (compareSellerOutChart.current) {
        compareSellerOutChart.current.dispose();
      }
      window.removeEventListener('resize', resizeFun);
    // }
 //
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
    if (sellersAndTime !== prevSellersAndTime.current || !compareSellerOutChart.current) {
      prevSellersAndTime.current = sellersAndTime;
      if (sellersAndTime.length > 1) {
        loadOut(sellersAndTime);
        compareSellerOutChart.current = echarts.init(chartEl.current);
        compareSellerOutChart.current.showLoading();
      }
      return;
    }
    const next = Tools.copyOption(out);
    if (next.series[0].data && next.series[0].data && next.series[0].data[0]) {

      next.legend.data.push(seller1, seller2);
      next.series[0].name = seller1;
      next.series[1].name = seller2;
      compareSellerOutChart.current.setOption(next);
      compareSellerOutChart.current.hideLoading();
    }
  }, [sellersAndTime, out, loadOut, seller1, seller2]);

  const rows = [];
  if (timeList) {
    timeList.forEach((item, i) => {
      rows.push(<tr key={i}><td>{timeList[i]}</td><td>{num1List[i]}</td><td>{parseInt(percent1List[i])}%</td><td>{num2List[i]}</td><td>{parseInt(percent2List[i])}%</td></tr>);
    });
  }

  return (<div>
    <div className="panel">
      <div className="panelHead">顾客跳出率对比&nbsp;<FaQuestion className="questionMark" />
        <div className="messageMark"><p>展示两个商家在同一时间段内的跳出率对比<br /></p></div></div>
      <div className="panelBody">
        <div ref={chartEl} className="compareSellerOutChart" />
      </div>
    </div>
    <div className="panel">
      <div className="panelHead">顾客跳出率对比明细</div>
      <div className="panelBody">
        <table className="Table">
          <thead>
            <tr><th>时间</th><th>{seller1}跳出人数</th><th>跳出率</th><th>{seller2}跳出人数</th><th>跳出率</th></tr>
          </thead>
          <tbody>
            {rows}
          </tbody>
        </table>
      </div>
    </div>
  </div>);
}

export default SellersOut;
