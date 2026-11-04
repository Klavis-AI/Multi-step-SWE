import React, {useCallback, useEffect, useReducer, useRef, useState} from 'react';
import {useSelector} from 'react-redux';
import echarts from 'echarts/lib/echarts';
import Calendar from '../../calendar';
// import '../../styles/calendar.scss';
import 'echarts/lib/chart/line';
import 'echarts/lib/chart/bar';
import 'echarts/lib/component/title';
import 'echarts/lib/component/legend';
import 'echarts/lib/component/tooltip';
import FaQuestion from 'react-icons/lib/fa/question';

import Tools from '../../tools';
import {useLazyCompareSellersOldOrNewInitQuery} from '../../../store/api';

function SellersOldOrNew({sellersAndTime}) {
  const OldOrNew = useSelector(state => state.compare.sellersOldOrNew);
  // Both fetches sit inside effects that are doing other imperative work: the
  // mount effect only fetches once it has two 商家 to compare, and the
  // componentWillReceiveProps effect fetches on the round that brings new ones
  // and then returns.  A lazy query is triggered where the thunk was dispatched.
  const [loadOldOrNew] = useLazyCompareSellersOldOrNewInitQuery();

  const [timeList, setTimeList] = useState('');
  const [newNum1List, setNewNum1List] = useState('');
  const [oldNum1List, setOldNum1List] = useState('');
  const [percent1List, setPercent1List] = useState('');
  const [newNum2List, setNewNum2List] = useState('');
  const [oldNum2List, setOldNum2List] = useState('');
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
  const compareSellerOldOrNewChart = useRef('');
  const resizeHandler = useRef(null);

  const resizeFun = useCallback(() => {
    if (resizeHandler.current) {
      clearTimeout(resizeHandler.current);
    }
    if (compareSellerOldOrNewChart.current) {
      resizeHandler.current = setTimeout(() => {
        compareSellerOldOrNewChart.current.resize();
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
    		// 	compareSellerOldOrNewChart.current = echarts.init(chartEl.current);
	     //    compareSellerOldOrNewChart.current.showLoading();
    		// }
    if (sellersAndTime.length > 1) {
      const names = sellersAndTime.split('/')[0].split(',');

      sellers.current.seller1 = names[0];
      sellers.current.seller2 = names[1];

      loadOldOrNew(sellersAndTime);
      compareSellerOldOrNewChart.current = echarts.init(chartEl.current);
      compareSellerOldOrNewChart.current.showLoading();
    }

    window.addEventListener('resize', resizeFun);
    return () => {
        // console.log('1componentWillUnmount');
      compareSellerOldOrNewChart.current.dispose();
      window.removeEventListener('resize', resizeFun);
    // }
 //
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
      loadOldOrNew(sellersAndTime);
      compareSellerOldOrNewChart.current = echarts.init(chartEl.current);
      compareSellerOldOrNewChart.current.showLoading();
      return;
    }
    prevSellersAndTime.current = sellersAndTime;
    const next = Tools.copyOption(OldOrNew);
    if (next.series[0].data && next.series[0].data && next.series[0].data[0]) {
      setTimeList(next.xAxis[0].data);
      setNewNum1List(next.xAxis[0].newNum1);
      setOldNum1List(next.xAxis[0].oldNum1);
      setOldNum2List(next.xAxis[0].oldNum2);
      setNewNum2List(next.xAxis[0].newNum2);
      setPercent1List(next.series[0].data);
      setPercent2List(next.series[1].data);

      next.legend.data.push(sellers.current.seller1, sellers.current.seller2);
      next.series[0].name = sellers.current.seller1;
      next.series[1].name = sellers.current.seller2;
      compareSellerOldOrNewChart.current.setOption(next);
      compareSellerOldOrNewChart.current.hideLoading();
    }
  }, [sellersAndTime, OldOrNew]);

  const {seller1, seller2} = sellers.current;
  const rows = [];
  if (timeList) {
    timeList.forEach((item, i) => {
      rows.push(<tr key={i}><td>{timeList[i]}</td><td>{newNum1List[i]}</td><td>{oldNum1List[i]}</td><td>{percent1List[i]}%</td><td>{newNum2List[i]}</td><td>{oldNum2List[i]}</td><td>{percent2List[i]}%</td></tr>);
    });
  }

  return (<div>
    <div className="panel">
      <div className="panelHead">顾客新顾客率对比&nbsp;<FaQuestion className="questionMark" />
        <div className="messageMark"><p>展示两个商家在同一时间段内的新顾客率对比<br /></p></div></div>
      <div className="panelBody">
        <div ref={chartEl} className="compareSellerOldOrNewChart" />
      </div>
    </div>
    <div className="panel">
      <div className="panelHead">顾客新顾客率对比明细</div>
      <div className="panelBody">
        <table className="Table">
          <thead>
            <tr><th>时间</th><th>{seller1}新顾客</th><th>老顾客</th><th>新顾客占比</th><th>{seller2}新顾客</th><th>老顾客</th><th>新顾客占比</th></tr>
          </thead>
          <tbody>
            {rows}
          </tbody>
        </table>
      </div>
    </div>
  </div>);
}

export default SellersOldOrNew;
