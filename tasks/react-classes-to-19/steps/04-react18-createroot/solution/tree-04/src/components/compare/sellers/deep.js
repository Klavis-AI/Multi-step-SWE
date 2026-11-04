import React, {useCallback, useEffect, useReducer, useRef, useState} from 'react';
import {useSelector} from 'react-redux';
import { useOutletContext } from 'react-router-dom';
import echarts from 'echarts/lib/echarts';
import Calendar from '../../calendar';
// import '../../styles/calendar.scss';
import 'echarts/lib/chart/line';
import 'echarts/lib/chart/bar';
import 'echarts/lib/component/title';
import 'echarts/lib/component/legend';
import 'echarts/lib/component/tooltip';
import { FaQuestion } from 'react-icons/fa';

import Tools from '../../tools';
import {useLazyCompareSellersDeepInitQuery} from '../../../store/api';

function SellersDeep() {
  const {sellersAndTime} = useOutletContext();
  const deep = useSelector(state => state.compare.sellersDeep);
  // Both fetches sit inside effects that are doing other imperative work: the
  // mount effect only fetches once it has two 商家 to compare, and the
  // componentWillReceiveProps effect fetches on the round that brings new ones
  // and then returns.  A lazy query is triggered where the thunk was dispatched.
  const [loadDeep] = useLazyCompareSellersDeepInitQuery();

  /* timeList was never in the constructor's state object - only componentWillReceiveProps
     ever put it there - so it starts undefined rather than empty, which is what the
     render guard below is written against. */
  const [timeList, setTimeList] = useState();
  const [num1List, setNum1List] = useState('');
  const [num2List, setNum2List] = useState('');
  const [percent1List, setPercent1List] = useState('');
  const [percent2List, setPercent2List] = useState('');

  /* seller1 and seller2 are read by render, so they are state - but
     componentDidMount wrote them straight onto the state object without a
     setState, and componentWillReceiveProps wrote them with one.  That is not a
     stylistic difference: on mount the names are set and nothing repaints, so
     the table header stays blank until the data arrives, while on a change of
     商家 the header takes the new names immediately, ahead of the data.  A
     mutable object in a ref plus a render requested at exactly the point that
     called setState is that component; two useState fields would repaint on
     mount as well. */
  const sellers = useRef({seller1: '', seller2: ''});   // 商家1/商家2，用于显示图表的legend
  const [, render] = useReducer(n => n + 1, 0);

  const chartEl = useRef(null);
  const compareSellerDeepChart = useRef('');
  const resizeHandler = useRef(null);

  /* setTimeout where every other copy of this function calls clearTimeout.  It
     schedules the pending timer id as if it were a callback, which throws
     nothing and cancels nothing, so a burst of resizes leaves every timer it
     started running.  Left as it is: it is what the component does. */
  const resizeFun = useCallback(() => {
    if (resizeHandler.current) {
      setTimeout(resizeHandler.current);
    }
    if (compareSellerDeepChart.current) {
      resizeHandler.current = setTimeout(() => {
        compareSellerDeepChart.current.resize();
      }, 100);
    }
  }, []);

    // componentWillMount(){
        // console.log('componentWillMount')


    // }
  useEffect(() => {
        // console.log('1componentDidMount');
        //  //dispatch(compareAction.allSellersTableInit());
    		// if(sellersAndTime){
    		// 	compareSellerDeepChart.current = echarts.init(chartEl.current);
	     //    compareSellerDeepChart.current.showLoading();
    		// }
    if (sellersAndTime.length > 1) {
      const names = sellersAndTime.split('/')[0].split(',');

      sellers.current.seller1 = names[0];
      sellers.current.seller2 = names[1];

      loadDeep(sellersAndTime);
      compareSellerDeepChart.current = echarts.init(chartEl.current);
      compareSellerDeepChart.current.showLoading();
    }

    window.addEventListener('resize', resizeFun);
    return () => {
        // console.log('1componentWillUnmount');
      // The chart is only initialised once there are two 商家 to compare, so this
      // cleanup can run with nothing to dispose - which is what StrictMode does
      // immediately after mounting, and what leaving the screen without choosing
      // a pair always did.
      if (compareSellerDeepChart.current) {
        compareSellerDeepChart.current.dispose();
      }
      window.removeEventListener('resize', resizeFun);
    };
  }, []);

  /* componentWillReceiveProps, transcribed rather than split in two, because the
     `return` is part of it: on the round that brings new 商家 the chart is torn
     down and re-initialised and the data branch is deliberately not reached.
     Holding the previous sellersAndTime in a ref is what makes the comparison
     this lifecycle made available to an effect; the mount skip is the
     lifecycle's own. */
  const prevSellersAndTime = useRef(sellersAndTime);
  const mounted = useRef(false);
  useEffect(() => {
    if (!mounted.current) { mounted.current = true; return; }
    if (sellersAndTime && sellersAndTime !== prevSellersAndTime.current) {
      prevSellersAndTime.current = sellersAndTime;
            // 获取商家名存入state
      const arr = sellersAndTime.split('/');
      const names = arr[0].split(',');

      sellers.current = {seller1: names[0], seller2: names[1]};
      render();
      loadDeep(sellersAndTime);
      compareSellerDeepChart.current = echarts.init(chartEl.current);
      compareSellerDeepChart.current.showLoading();
      return;
    }
    prevSellersAndTime.current = sellersAndTime;
    const next = Tools.copyOption(deep);
    if (next.series[0].data && next.series[0].data && next.series[0].data[0]) {
      setTimeList(next.xAxis[0].data);
      setNum1List(next.xAxis[0].deepNum1);
      setNum2List(next.xAxis[0].deepNum2);
      setPercent1List(next.series[0].data);
      setPercent2List(next.series[1].data);
      next.legend.data.push(sellers.current.seller1, sellers.current.seller2);
      next.series[0].name = sellers.current.seller1;
      next.series[1].name = sellers.current.seller2;
      compareSellerDeepChart.current.setOption(next);
      compareSellerDeepChart.current.hideLoading();
    }
  }, [sellersAndTime, deep]);

  const {seller1, seller2} = sellers.current;
  const rows = [];
  if (timeList) {
    timeList.forEach((item, i) => {
      rows.push(<tr key={i}><td>{timeList[i]}</td><td>{num1List[i]}</td><td>{parseInt(percent1List[i])}%</td><td>{num2List[i]}</td><td>{parseInt(percent2List[i])}%</td></tr>);
    });
  }

  return (<div>
    <div className="panel">
      <div className="panelHead">顾客深访率对比&nbsp;<FaQuestion className="questionMark" />
        <div className="messageMark"><p>展示两个商家在同一时间段内的深访率对比<br /></p></div></div>
      <div className="panelBody">
        <div ref={chartEl} className="compareSellerDeepChart" />
      </div>
    </div>
    <div className="panel">
      <div className="panelHead">顾客深访率对明细</div>
      <div className="panelBody">
        <table className="Table">
          <thead>
            <tr><th>时间</th><th>{seller1}深访人数</th><th>深访率</th><th>{seller2}深访人数</th><th>深访率</th></tr>
          </thead>
          <tbody>
            {rows}
          </tbody>
        </table>
      </div>
    </div>
  </div>);
}

export default SellersDeep;
