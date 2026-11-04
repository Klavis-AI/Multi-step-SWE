
import React, {useEffect, useRef, useState} from 'react';
import {useSelector} from 'react-redux';
import echarts from 'echarts/lib/echarts';
import Calendar from '../../calendar';
import Tools from '../../tools';
import 'echarts/lib/chart/line';
import 'echarts/lib/chart/bar';
import 'echarts/lib/component/title';
import 'echarts/lib/component/legend';
import 'echarts/lib/component/tooltip';
import 'echarts/lib/component/grid';
import 'echarts/lib/component/toolbox';
import { FaQuestion } from 'react-icons/fa';

import {useLazyCompareNumAvgInitQuery} from '../../../store/api';

/* state.Data was built empty and never written again, so the table branch that
   reads it below has never run.  It stays a constant for the same reason it was
   never a useState: nothing can change it. */
const Data = [];

/* The range the panel opens on: the initial value of the state a range
   click replaces, and the range the first fetch asks for. */
const DEFAULT_RANGE = 'day';

function CustomerAvg() {
  const customerAvg = useSelector(state => state.compare.customerAvg);
  // Both fetches are imperative: one runs inside the mount effect after it has
  // worked out today's date, the other inside the search handler with whatever
  // the two calendars hold.  A lazy query is triggered the same way the thunk
  // was dispatched, and the trigger is stable across renders.
  const [loadNumAvg] = useLazyCompareNumAvgInitQuery();

  const [selectTime, setSelectTime] = useState(DEFAULT_RANGE);

  /* The table's values were 3 useState that the effect below assigned every
     time the slice changed, which is a render's worth of work done a render
     late: each one is a read of the response the store already holds.  Render
     works them out where they are used, and the state they replace goes with
     them - a useState whose only writer was that effect was never state. */
  const timeList = customerAvg.xAxis[0].data;
  const num1List = customerAvg.series[0].data;
  const num2List = customerAvg.series[1].data;

  /* time, time1 and time2 were written straight onto the state object in
     componentDidMount and in search, without a setState in sight, and nothing
     renders them - time1 and time2 are only read by the commented-out legend
     below.  They were instance fields wearing state's clothes. */
  const time = useRef('');          // 要请求的time参数，有多个
  const time1 = useRef('');           // 时间1，用于显示图表的legend
  const time2 = useRef('');              // 时间2,用于显示图表的legend

  const selectTime1 = useRef(null);
  const selectTime2 = useRef(null);
  const chartEl = useRef(null);
  const compareCustomerAvgChart = useRef(null);
  const resizeHandler = useRef(null);

    // componentWillMount(){
        // console.log('componentWillMount')

 //        dispatch(compareAction.comparenumInit(time.current,chartPage));

    // }

  /* The first fetch asks for the range the panel opens on, which is the range
     `selectTime` is initialised to; naming it once is what lets this effect say
     it reads nothing from the render but the query trigger, rather than list a
     piece of state that changing must not re-run it.  The resize listener is
     declared where it is used for the same reason. */
  useEffect(() => {
        // console.log('componentDidMount');
        // 	//dispatch(compareAction.allSellersTableInit());
    const getTime = Tools.getTime();
    time.current = getTime;
    time1.current = getTime.split(',')[0];
    time2.current = getTime.split(',')[1];
    loadNumAvg({time: Tools.changeTime(time.current), range: DEFAULT_RANGE});   // 默认无参，可选

    const input1 = selectTime1.current.getElementsByClassName('calendar')[0].getElementsByTagName('input')[0];
    input1.value = time.current.split(',')[0];

    compareCustomerAvgChart.current = echarts.init(chartEl.current);
    compareCustomerAvgChart.current.showLoading();

    const resizeFun = () => {
      if (resizeHandler.current) {
        clearTimeout(resizeHandler.current);
      }
      if (compareCustomerAvgChart.current) {
        resizeHandler.current = setTimeout(() => {
          compareCustomerAvgChart.current.resize();
        }, 100);
      }
    };

    window.addEventListener('resize', resizeFun);
    return () => {
		// console.log('componentWillUnmount');
      compareCustomerAvgChart.current.dispose();
      window.removeEventListener('resize', resizeFun);
	// }
 //
    };
  }, [loadNumAvg]);

  /* componentWillReceiveProps, guarded on "the props object is a new one" -
     which under connect it always was, so the only thing that could actually
     have moved is the slice this reads.  What is left of it is the chart; the
     mount skip went out with the state assignments, and nothing needs it back.
     The option the reducer starts from has the axis and the series in it with
     no data in either, so the first render reads undefined rather than
     throwing, and the guard below is false until a response lands. */
  useEffect(() => {
    const next = Tools.copyOption(customerAvg);  // 当props改变时才触发。只改变selectTime不触发
    if (next.series[0].data && next.series[0].data && next.series[0].data[0]) {
                // 判断若是多天的，则legend不能为日期，直接写：时间一、时间二 ？
                // if(selectTime!=='day' /*&& selectTime!=='hour'*/){
      next.legend.data.push('时间一', '时间二');
      next.series[0].name = '时间一';
      next.series[1].name = '时间二';
                // }else{
                //     next.legend.data.push(time1.current,time2.current);
                //     next.series[0].name = time1.current;
                //     next.series[1].name = time2.current;
                // }
      compareCustomerAvgChart.current.setOption(next);
      compareCustomerAvgChart.current.hideLoading();
    }
  }, [customerAvg]);
    // componentWillUpdate(nextProps){
        // console.log('-=componentWillUpdate')
    // }
    // componentDidUpdate(prevProps,prevState){
        // console.log('..componentDidUpdate')

 //
    // }
	// componentWillReceiveProps(){
	// }
  const changeTime = (e) => {
    if (e.target.className === 'active') {
      return;
    }
    switch (e.target.innerText) {
        /* case '时':
          time.current='hour';
          setSelectTime('hour');
          return;*/
      case '日':
        time.current = 'day';
        setSelectTime('day');
        return;
      case '周':
        time.current = 'week';
        setSelectTime('week');
        return;
      case '月':
        time.current = 'month';
        setSelectTime('month');

    }
  };
  const search = () => {
    let searchTime1 = selectTime1.current.getElementsByClassName('calendar')[0].getElementsByTagName('input')[0].value;
    let searchTime2 = selectTime2.current.getElementsByClassName('calendar')[0].getElementsByTagName('input')[0].value;
    searchTime1 = Tools.changeTime(searchTime1);
    searchTime2 = Tools.changeTime(searchTime2);
        // 去除红色警示框ClassName
    selectTime1.current.className = selectTime1.current.className.replace(' selectTimeError', '');
    selectTime2.current.className = selectTime1.current.className.replace(' selectTimeError', '');
    const ms1 = new Date(searchTime1).getTime();
    const ms2 = new Date(searchTime2).getTime();
    const now = Date.now();
    let error = false;
    if (ms1 > now) {  // 选择时间超过当前时间，若选择的是今日，也不会超过当前时间
      selectTime1.current.className += ' selectTimeError';// 添加红色警示框ClassName
      error = true;
    }
    if (ms1 >= ms2) { // 时间1大于等于时间2，错误
      selectTime2.current.className += ' selectTimeError';
      error = true;
    }
    if (ms2 > now) {  // 选择时间超过当前时间，若选择的是今日，也不会超过当前时间
      selectTime2.current.className += ' selectTimeError';// 添加红色警示框ClassName
      error = true;
    }
    if (error) {
      return false;
    }
        // 日历选择没有错误，得到时间范围,发请求,并保存时间，放入图表legend
    loadNumAvg({time: `${searchTime1},${searchTime2}`, range: selectTime});
    time1.current = searchTime1;
    time2.current = searchTime2;
  };

  const rows = [];
  let rowTime1,
    rowTime2;
  if (timeList) {
    timeList.forEach((item, i) => {
      rowTime1 = timeList[i].split('/')[0];
      rowTime2 = timeList[i].split('/')[1];
      rows.push(<tr key={i}><td>{rowTime1}</td><td>{num1List[i]}</td><td>{rowTime2}</td><td>{num2List[i]}</td></tr>);
    });
  }
  if (Data.series && Data.series[0].data) {
    const sellerName = Data.xAxis[0].data;
    const sellerNum = Data.series[0].data;
    const sellerPer = Data.series[1].data;
    sellerName.forEach((item, index) => {
      rows.push(<tr key={index}><th>{index + 1}</th><td>{item}</td><td>{sellerNum[index]}{sellerPer[index] > 0 ? <span className="up">&nbsp;↑</span> : <span className="down">&nbsp;↓</span>}</td><td className={sellerPer[index] > 0 ? 'up' : 'down'}>{sellerPer[index]}%</td></tr>);
    });
  }

  return (<div className="chartWrapper">


    <div className="selectOption inline">
                  时间：
                  <div className="selectTime1" ref={selectTime1}><Calendar /></div>
                  &nbsp;&nbsp;&nbsp;对比时间：&nbsp;
                  <div className="selectTime1" ref={selectTime2}><Calendar /></div>
                  &nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;对比时间范围：
                  <div className="quickSelect defaultCursor">
                    <ul>
                      {/* <li><a className={selectTime=='hour'?'active':''} onClick={changeTime}>时</a></li>*/}
                      <li><a className={selectTime == 'day' ? 'active' : ''} onClick={changeTime}>日</a></li>
                      <li><a className={selectTime == 'week' ? 'active' : ''} onClick={changeTime}>周</a></li>
                      <li><a className={selectTime == 'month' ? 'active' : ''} onClick={changeTime}>月</a></li>
                    </ul>
                  </div>
      <div className="selectClick">
        <input type="button" value="查询" onClick={search} />
      </div>
    </div>

    <div className="panel">
      <div className="panelHead">客流量对比&nbsp;<FaQuestion className="questionMark" />
        <div className="messageMark"><p>展示商城在两个时间段内的客流平均值对比<br /></p></div></div>
      <div className="panelBody">
        <div ref={chartEl} className="compareCustomerAvgChart" />
      </div>
    </div>
    <div className="panel">
      <div className="panelHead">客流量对比明细</div>
      <div className="panelBody">
        <table className="Table">
          <thead>
            <tr><th>时间一</th><th>客流量</th><th>时间二</th><th>客流量</th></tr>
          </thead>
          <tbody>
            {rows}
          </tbody>
        </table>
      </div>
    </div>
  </div>);
}

export default CustomerAvg;

