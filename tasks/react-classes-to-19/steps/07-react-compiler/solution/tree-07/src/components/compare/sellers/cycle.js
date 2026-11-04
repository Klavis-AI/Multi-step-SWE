import React, {useEffect, useRef, useState} from 'react';
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
import 'echarts/lib/component/grid';
import 'echarts/lib/component/toolbox';
import { FaQuestion } from 'react-icons/fa';

import Tools from '../../tools';
import {useLazyCompareSellersCycleInitQuery} from '../../../store/api';

function Cycle() {
  const {sellersAndTime} = useOutletContext();
  const cycle = useSelector(state => state.compare.sellersCycle);
  // Both fetches sit inside effects that are doing other imperative work: the
  // mount effect only fetches once it has two 商家 to compare, and the
  // componentWillReceiveProps effect fetches on the round that brings new ones
  // and then returns.  A lazy query is triggered where the thunk was dispatched.
  const [loadCycle] = useLazyCompareSellersCycleInitQuery();

  const [timeList, setTimeList] = useState('');
  const [num1List, setNum1List] = useState('');
  const [num2List, setNum2List] = useState('');
  const [total1, setTotal1] = useState('');
  const [total2, setTotal2] = useState('');

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
  const compareSellerCycleChart = useRef('');
  const resizeHandler = useRef(null);

  const resizeFun = () => {
    if (resizeHandler.current) {
      clearTimeout(resizeHandler.current);
    }
    if (compareSellerCycleChart.current) {
      resizeHandler.current = setTimeout(() => {
        compareSellerCycleChart.current.resize();
      }, 100);
    }
  };

    // componentWillMount(){
        // console.log('componentWillMount')


    // }
  useEffect(() => {
        // console.log('1componentDidMount');
    //  //dispatch(compareAction.allSellersTableInit());
    		// if(sellersAndTime){
    		// 	compareSellerCycleChart.current = echarts.init(chartEl.current);
	     //    compareSellerCycleChart.current.showLoading();
    		// }
    if (sellersAndTime.length > 1) {
loadCycle(sellersAndTime);
      compareSellerCycleChart.current = echarts.init(chartEl.current);
      compareSellerCycleChart.current.showLoading();
    }

    window.addEventListener('resize', resizeFun);
    return () => {
        // console.log('1componentWillUnmount');
      // The chart is only initialised once there are two 商家 to compare, so this
      // cleanup can run with nothing to dispose - which is what StrictMode does
      // immediately after mounting, and what leaving the screen without choosing
      // a pair always did.
      if (compareSellerCycleChart.current) {
        compareSellerCycleChart.current.dispose();
      }
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

      loadCycle(sellersAndTime);
      compareSellerCycleChart.current = echarts.init(chartEl.current);
      compareSellerCycleChart.current.showLoading();
      return;
    }
    prevSellersAndTime.current = sellersAndTime;
    const next = Tools.copyOption(cycle);
    if (next.series[0].data && next.series[0].data && next.series[0].data[0]) {
      const nextNum1List = next.series[0].data;
      const nextNum2List = next.series[1].data;
      setTimeList(next.xAxis[0].data);
      setNum1List(nextNum1List);
      setNum2List(nextNum2List);
      setTotal1(nextNum1List.reduce((x, y) => (parseInt(x) + parseInt(y))));
      setTotal2(nextNum2List.reduce((x, y) => (parseInt(x) + parseInt(y))));

      next.legend.data.push(seller1, seller2);
      next.series[0].name = seller1;
      next.series[1].name = seller2;
      compareSellerCycleChart.current.setOption(next);
      compareSellerCycleChart.current.hideLoading();
    }
  }, [sellersAndTime, cycle]);

  const rows = [];
  let percent1,
    percent2;
  if (timeList) {
    timeList.forEach((item, i) => {
      if (!total1) {
        percent1 = 0;
      } else {
        percent1 = parseInt((num1List[i] / total1) * 100);
      }
      if (!total2) {
        percent2 = 0;
      } else {
        percent2 = parseInt((num2List[i] / total2) * 100);
      }
      rows.push(<tr key={i}><td>{timeList[i]}</td><td>{num1List[i]}</td><td>{percent1}%</td><td>{num2List[i]}</td><td>{percent2}%</td></tr>);
    });
  }

  return (<div>
    <div className="panel">
      <div className="panelHead">顾客来访周期对比&nbsp;<FaQuestion className="questionMark" />
        <div className="messageMark"><p>展示两个商家在同一时间段内的来访周期对比<br /></p></div></div>
      <div className="panelBody">
        <div ref={chartEl} className="compareSellerCycleChart" />
      </div>
    </div>
    <div className="panel">
      <div className="panelHead">顾客来访周期对比明细</div>
      <div className="panelBody">
        <table className="Table">
          <thead>
            <tr><th>来访周期</th><th>{seller1}人数</th><th>所占比例</th><th>{seller2}人数</th><th>所占比例</th></tr>
          </thead>
          <tbody>
            {rows}
          </tbody>
        </table>
      </div>
    </div>
  </div>);
}

export default Cycle;
