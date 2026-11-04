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
import compareAction from '../../../actions/compareAction';

function TimeSection({sellersAndTime}) {
  const timeSection = useSelector(state => state.getIn(['d', 'sellersTimeSection']));
  const dispatch = useDispatch();

  /* None of these five were in the constructor's state object - only
     componentWillReceiveProps ever put them there - so they start undefined
     rather than empty, which is what the render guard below is written against. */
  const [timeList, setTimeList] = useState();
  const [num1List, setNum1List] = useState();
  const [num2List, setNum2List] = useState();
  const [total1, setTotal1] = useState();
  const [total2, setTotal2] = useState();

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
  const compareSellerTimeSectionChart = useRef('');
  const resizeHandler = useRef(null);

  const resizeFun = useCallback(() => {
    if (resizeHandler.current) {
      clearTimeout(resizeHandler.current);
    }
    if (compareSellerTimeSectionChart.current) {
      resizeHandler.current = setTimeout(() => {
        compareSellerTimeSectionChart.current.resize();
      }, 100);
    }
  }, []);

    // componentWillMount(){
        // console.log('componentWillMount')


    // }
  useEffect(() => {
        // console.log('1 componentDidMount');
    //  //dispatch(compareAction.allSellersTableInit());
    		// if(sellersAndTime){
    		// 	compareSellerTimeSectionChart.current = echarts.init(chartEl.current);
	     //    compareSellerTimeSectionChart.current.showLoading();
    		// }
    if (sellersAndTime.length > 1) {
      const names = sellersAndTime.split('/')[0].split(',');

      sellers.current.seller1 = names[0];
      sellers.current.seller2 = names[1];

      dispatch(compareAction.sellersTimeSectionInit(sellersAndTime));
      compareSellerTimeSectionChart.current = echarts.init(chartEl.current);
      compareSellerTimeSectionChart.current.showLoading();
    }

    window.addEventListener('resize', resizeFun);
    return () => {
        // console.log('1componentWillUnmount');
      compareSellerTimeSectionChart.current.dispose();
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
    	// 有参数传入，才发送请求渲染图表。防止无限循环发送请求，要两次props对比，不同才发
    if (sellersAndTime && sellersAndTime !== prevSellersAndTime.current) {
      prevSellersAndTime.current = sellersAndTime;
            // 获取商家名存入state
      const arr = sellersAndTime.split('/');
      const names = arr[0].split(',');

      sellers.current = {seller1: names[0], seller2: names[1]};
      render();

      dispatch(compareAction.sellersTimeSectionInit(sellersAndTime));
      compareSellerTimeSectionChart.current = echarts.init(chartEl.current);
      compareSellerTimeSectionChart.current.showLoading();
      return;
    }
    prevSellersAndTime.current = sellersAndTime;
    const next = timeSection.toJS();
    if (next.series[0].data && next.series[0].data && next.series[0].data[0]) {
      const nextNum1List = next.series[0].data;
      const nextNum2List = next.series[1].data;
      setTimeList(next.xAxis[0].data);
      setNum1List(nextNum1List);
      setNum2List(nextNum2List);
      setTotal1(nextNum1List.reduce((x, y) => (parseInt(x) + parseInt(y))));
      setTotal2(nextNum2List.reduce((x, y) => (parseInt(x) + parseInt(y))));
      next.legend.data.push(sellers.current.seller1, sellers.current.seller2);
      next.series[0].name = sellers.current.seller1;
      next.series[1].name = sellers.current.seller2;

      compareSellerTimeSectionChart.current.setOption(next);
      compareSellerTimeSectionChart.current.hideLoading();
    }
  }, [sellersAndTime, timeSection]);

  const {seller1, seller2} = sellers.current;
  const rows = [];
  let percent1,
    percent2;
        // console.log(total1,total1);
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
      <div className="panelHead">顾客各时间段人数对比</div>
      <div className="panelBody">
        <div ref={chartEl} className="compareSellerTimeSectionChart" />
      </div>
    </div>
    <div className="panel">
      <div className="panelHead">顾客各时间段人数信息</div>
      <div className="panelBody">
        <table className="Table">
          <thead>
            <tr><th>时间段</th><th>{seller1}人数</th><th>所占比例</th><th>{seller2}人数</th><th>所占比例</th></tr>
          </thead>
          <tbody>
            {rows}
          </tbody>
        </table>
      </div>
    </div>
  </div>);
}

export default TimeSection;
