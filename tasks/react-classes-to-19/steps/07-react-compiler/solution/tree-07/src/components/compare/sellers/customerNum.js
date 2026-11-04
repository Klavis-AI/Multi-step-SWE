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
import {useLazyCompareSellersNumInitQuery} from '../../../store/api';

function SellersNum() {
  const {sellersAndTime} = useOutletContext();
  const sellersNum = useSelector(state => state.compare.sellersNum);
  // Both fetches sit inside effects that are doing other imperative work: the
  // mount effect only fetches once it has two 商家 to compare, and the
  // componentWillReceiveProps effect fetches on the round that brings new ones
  // and then returns.  A lazy query is triggered where the thunk was dispatched.
  const [loadNum] = useLazyCompareSellersNumInitQuery();

  const [timeList, setTimeList] = useState('');
  const [num1List, setNum1List] = useState('');
  const [num2List, setNum2List] = useState('');
  const [percent1List, setPercent1List] = useState('');
  const [percent2List, setPercent2List] = useState('');

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
  const compareSellerNumChart = useRef('');
  const resizeHandler = useRef(null);

  const resizeFun = () => {
    if (resizeHandler.current) {
      clearTimeout(resizeHandler.current);
    }
    if (compareSellerNumChart.current) {
      resizeHandler.current = setTimeout(() => {
        compareSellerNumChart.current.resize();
      }, 100);
    }
  };

    // componentWillMount(){
        // console.log('componentWillMount-- ')


    // }
  useEffect(() => {
        // console.log('1componentDidMount');
    //  //dispatch(compareAction.allSellersTableInit());
    		// if(sellersAndTime){
    		// 	compareSellerNumChart.current = echarts.init(chartEl.current);
	     //    compareSellerNumChart.current.showLoading();
    		// }
        // 进入此路由时，已经选择了时间参数，可以立即渲染图表
    if (sellersAndTime.length > 1) {
loadNum(sellersAndTime);
      compareSellerNumChart.current = echarts.init(chartEl.current);
      compareSellerNumChart.current.showLoading();
    }

    window.addEventListener('resize', resizeFun);
    return () => {
        // console.log('1componentWillUnmount');
      // The chart is only initialised once there are two 商家 to compare, so this
      // cleanup can run with nothing to dispose - which is what StrictMode does
      // immediately after mounting, and what leaving the screen without choosing
      // a pair always did.
      if (compareSellerNumChart.current) {
        compareSellerNumChart.current.dispose();
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
        // 有参数传入，才发送请求渲染图表。防止无限循环发送请求，要两次props对比，不同才发
    if (sellersAndTime && sellersAndTime !== prevSellersAndTime.current) {
      prevSellersAndTime.current = sellersAndTime;
      loadNum(sellersAndTime);
      compareSellerNumChart.current = echarts.init(chartEl.current);
      compareSellerNumChart.current.showLoading();
      return;
    }
    prevSellersAndTime.current = sellersAndTime;
    const next = Tools.copyOption(sellersNum);
    if (next.series[0].data && next.series[0].data[0]) {
      setTimeList(next.xAxis[0].data);
      setNum1List(next.series[0].data);
      setNum2List(next.series[1].data);
      setPercent1List(next.xAxis[0].percent1);
      setPercent2List(next.xAxis[0].percent2);
      next.legend.data.push(seller1, seller2);
      next.series[0].name = seller1;
      next.series[1].name = seller2;
      compareSellerNumChart.current.setOption(next);
      compareSellerNumChart.current.hideLoading();
    }
  }, [sellersAndTime, sellersNum]);

  const rows = [];
  if (timeList) {
    timeList.forEach((item, i) => {
      rows.push(<tr key={i}><td>{timeList[i]}</td><td>{num1List[i]}</td><td>{percent1List[i]}%</td><td>{num2List[i]}</td><td>{percent2List[i]}%</td></tr>);
    });
  }

  return (<div>
    <div className="panel">
      <div className="panelHead">客流量峰值对比&nbsp;<FaQuestion className="questionMark" />
        <div className="messageMark"><p>展示两个商家在同一时间段内的客流峰值对比<br /></p></div></div>
      <div className="panelBody">
        <div ref={chartEl} className="compareSellerNumChart" />
      </div>
    </div>
    <div className="panel">
      <div className="panelHead">客流量峰值对比明细</div>
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

export default SellersNum;
