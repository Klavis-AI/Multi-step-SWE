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

import {useLazyCompareOutInitQuery} from '../../../store/api';

function Out() {
  const out = useSelector(state => state.compare.out);
  // Both fetches are imperative: one runs inside the mount effect after it has
  // worked out today's date, the other inside the search handler with whatever
  // the two calendars hold.  A lazy query is triggered the same way the thunk
  // was dispatched, and the trigger is stable across renders.
  const [loadOut] = useLazyCompareOutInitQuery();

  const [selectTime, setSelectTime] = useState('day');
  const [timeList, setTimeList] = useState('');
  const [num1List, setNum1List] = useState('');
  const [num2List, setNum2List] = useState('');
  const [percent1List, setPercent1List] = useState('');
  const [percent2List, setPercent2List] = useState('');

  /* Written straight onto the state object, never through setState, and never
     rendered - time1 and time2 are only read by the commented-out legend below.
     They were instance fields wearing state's clothes.  Data was the same story
     with nothing left that reads it at all. */
  const time = useRef('');          // 要请求的time参数，有多个
  const time1 = useRef('');           // 时间1，用于显示图表的legend
  const time2 = useRef('');              // 时间2,用于显示图表的legend

  const selectTime1 = useRef(null);
  const selectTime2 = useRef(null);
  const chartEl = useRef(null);
  const compareOutChart = useRef(null);
  const resizeHandler = useRef(null);

  const resizeFun = () => {
    if (resizeHandler.current) {
      clearTimeout(resizeHandler.current);
    }
    if (compareOutChart.current) {
      resizeHandler.current = setTimeout(() => {
        compareOutChart.current.resize();
      }, 100);
    }
  };

    // componentWillMount(){
        // console.log('componentWillMount')

 //        dispatch(compareAction.compareCustomerNumInit(time.current,chartPage));

    // }

  useEffect(() => {
        // console.log('componentDidMount');
    //  //dispatch(compareAction.allSellersTableInit());
    const getTime = Tools.getTime();
    time.current = getTime;
    time1.current = getTime.split(',')[0];
    time2.current = getTime.split(',')[1];
    loadOut({time: Tools.changeTime(time.current), range: selectTime});   // 默认无参，可选

    const input1 = selectTime1.current.getElementsByClassName('calendar')[0].getElementsByTagName('input')[0];
    input1.value = time.current.split(',')[0];

    compareOutChart.current = echarts.init(chartEl.current);
    compareOutChart.current.showLoading();

    window.addEventListener('resize', resizeFun);
    return () => {
        // console.log('componentWillUnmount');
      compareOutChart.current.dispose();
      window.removeEventListener('resize', resizeFun);
    };
  }, []);

  /* componentWillReceiveProps, with no guard at all - it ran on every update
     this component was given, which under connect is every update the slice
     moved.  The skip is that lifecycle's own, and it is load-bearing: on the
     first render the slice is still the empty one the reducer starts from and
     series[0] would throw. */
  const mounted = useRef(false);
  useEffect(() => {
    if (!mounted.current) { mounted.current = true; return; }
    const next = Tools.copyOption(out);
    if (next.series[0].data && next.series[0].data && next.series[0].data[0]) {
      setTimeList(next.xAxis[0].data);
      setNum1List(next.xAxis[0].out1Num);
      setNum2List(next.xAxis[0].out2Num);
      setPercent1List(next.series[0].data);
      setPercent2List(next.series[1].data);
            // next.legend.data.push(time1.current,time2.current);
            // next.series[0].name = time1.current;
            // next.series[1].name = time2.current;
      next.legend.data.push('时间一', '时间二');
      next.series[0].name = '时间一';
      next.series[1].name = '时间二';
      compareOutChart.current.setOption(next);
      compareOutChart.current.hideLoading();
    }
  }, [out]);

  const changeTime = (e) => {
    if (e.target.className === 'active') {
      return;
    }
    switch (e.target.innerText) {
        // case '时':
        //   time.current='hour';
        //   setSelectTime('hour');
        //   return;
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
    if (ms1 >= ms2) { // 时间1大于时间2，错误
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
    loadOut({time: `${searchTime1},${searchTime2}`, range: selectTime});
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
      rows.push(<tr key={i}><td>{rowTime1}</td><td>{num1List[i]}</td><td>{percent1List[i]}%</td><td>{rowTime2}</td><td>{num2List[i]}</td><td>{percent2List[i]}%</td></tr>);
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
      <div className="panelHead">跳出率对比&nbsp;<FaQuestion className="questionMark" />
        <div className="messageMark"><p>展示商城在两个时间段内的跳出率对比<br /></p></div></div>
      <div className="panelBody">
        <div ref={chartEl} className="compareOutChart" />
      </div>
    </div>
    <div className="panel">
      <div className="panelHead">跳出率对比明细</div>
      <div className="panelBody">
        <table className="Table">
          <thead>
            <tr><th>时间一</th><th>跳出量</th><th>跳出率</th><th>时间二</th><th>跳出量</th><th>跳出率</th></tr>
          </thead>
          <tbody>
            {rows}
          </tbody>
        </table>
      </div>
    </div>
  </div>);
}

export default Out;
