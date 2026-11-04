import React, {useCallback, useEffect, useReducer, useRef, useState} from 'react';
import {useDispatch, useSelector} from 'react-redux';
import echarts from 'echarts/lib/echarts';
import Calendar from '../../calendar';
// import '../../styles/calendar.scss';
import 'echarts/lib/chart/line';
import 'echarts/lib/chart/bar';
import 'echarts/lib/component/title';
import 'echarts/lib/component/legend';
import 'echarts/lib/component/tooltip';
import FaQuestion from 'react-icons/lib/fa/question';

import compareAction from '../../../actions/compareAction';

function SellersAvg({sellersAndTime}) {
  const sellersAvg = useSelector(state => state.getIn(['d', 'sellersAvg']));
  const dispatch = useDispatch();

  const [timeList, setTimeList] = useState('');
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
  const compareSellerAvgChart = useRef('');
  const resizeHandler = useRef(null);

  const resizeFun = useCallback(() => {
    if (resizeHandler.current) {
      clearTimeout(resizeHandler.current);
    }
    if (compareSellerAvgChart.current) {
      resizeHandler.current = setTimeout(() => {
        compareSellerAvgChart.current.resize();
      }, 100);
    }
  }, []);

  /* componentWillMount had a body of one commented-out log and nothing else.
     There is no effect to write for a lifecycle that did nothing. */
        // console.log('componentWillMount-- ')

  useEffect(() => {
        // 进入此路由时，已经选择了时间参数，可以立即渲染图表
    if (sellersAndTime.length > 1) {
      const names = sellersAndTime.split('/')[0].split(',');
      sellers.current.seller1 = names[0];
      sellers.current.seller2 = names[1];

      dispatch(compareAction.sellersAvgInit(sellersAndTime));
      compareSellerAvgChart.current = echarts.init(chartEl.current);
      compareSellerAvgChart.current.showLoading();
    }

    window.addEventListener('resize', resizeFun);
    return () => {
        // console.log('1componentWillUnmount');
      compareSellerAvgChart.current.dispose();
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
            // 获取商家名存入state
      const arr = sellersAndTime.split('/');

      const names = arr[0].split(',');
      sellers.current = {seller1: names[0], seller2: names[1]};
      render();
      dispatch(compareAction.sellersAvgInit(sellersAndTime));
      compareSellerAvgChart.current = echarts.init(chartEl.current);
      compareSellerAvgChart.current.showLoading();
      return;
    }
    prevSellersAndTime.current = sellersAndTime;
    const next = sellersAvg.toJS();
    if (next.series[0].data && next.series[0].data[0]) {
      setTimeList(next.xAxis[0].data);
      setNum1List(next.series[0].data);
      setNum2List(next.series[1].data);
      setPercent1List(next.xAxis[0].percent1);
      setPercent2List(next.xAxis[0].percent2);
      next.legend.data.push(sellers.current.seller1, sellers.current.seller2);
      next.series[0].name = sellers.current.seller1;
      next.series[1].name = sellers.current.seller2;
      compareSellerAvgChart.current.setOption(next);
      compareSellerAvgChart.current.hideLoading();
    }
  }, [sellersAndTime, sellersAvg]);

  const {seller1, seller2} = sellers.current;
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
