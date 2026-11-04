import React, {useCallback, useEffect, useRef, useState} from 'react';
import {useSelector} from 'react-redux';
import echarts from 'echarts/lib/echarts';
import Calendar from '../../calendar';
import Tools from '../../tools';
import 'echarts/lib/chart/line';
import 'echarts/lib/chart/bar';
import 'echarts/lib/component/title';
import 'echarts/lib/component/legend';
import 'echarts/lib/component/tooltip';
import { FaQuestion } from 'react-icons/fa';

import {useLazyCompareOldOrNewInitQuery} from '../../../store/api';

function OldOrNew() {
  const oldOrNew = useSelector(state => state.compare.oldOrNew);
  // Both fetches are imperative: one runs inside the mount effect after it has
  // worked out today's date, the other inside the search handler with whatever
  // the two calendars hold.  A lazy query is triggered the same way the thunk
  // was dispatched, and the trigger is stable across renders.
  const [loadOldOrNew] = useLazyCompareOldOrNewInitQuery();

  const [selectTime, setSelectTime] = useState('day');
  /* timeList was never in the constructor's state object - only componentWillReceiveProps
     ever put it there - so it starts undefined rather than empty, which is what the
     render guard below is written against. */
  const [timeList, setTimeList] = useState();
  const [new1List, setNew1List] = useState('');
  const [old1List, setOld1List] = useState('');
  const [percent1List, setPercent1List] = useState('');
  const [new2List, setNew2List] = useState('');
  const [old2List, setOld2List] = useState('');
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
  const compareOldOrNewChart = useRef(null);
  const resizeHandler = useRef(null);

  const resizeFun = useCallback(() => {
    if (resizeHandler.current) {
      clearTimeout(resizeHandler.current);
    }
    if (compareOldOrNewChart.current) {
      resizeHandler.current = setTimeout(() => {
        compareOldOrNewChart.current.resize();
      }, 100);
    }
  }, []);

  /* componentWillMount had a body of one commented-out log and nothing else.
     There is no effect to write for a lifecycle that did nothing. */
        // console.log('componentWillMount')

  useEffect(() => {
        // console.log('componentDidMount');
    //  //dispatch(compareAction.allSellersTableInit());
    const getTime = Tools.getTime();
    time.current = getTime;
    time1.current = getTime.split(',')[0];
    time2.current = getTime.split(',')[1];
    loadOldOrNew({time: Tools.changeTime(time.current), range: selectTime});   // 默认无参，可选

    const input1 = selectTime1.current.getElementsByClassName('calendar')[0].getElementsByTagName('input')[0];
    input1.value = time.current.split(',')[0];

    compareOldOrNewChart.current = echarts.init(chartEl.current);
    compareOldOrNewChart.current.showLoading();

    window.addEventListener('resize', resizeFun);
    return () => {
        // console.log('componentWillUnmount');
      compareOldOrNewChart.current.dispose();
      window.removeEventListener('resize', resizeFun);
    // }
 //
    };
  }, []);

  /* componentWillReceiveProps, guarded on "the props object is a new one" -
     which under connect it always was, so the only thing that could actually
     have moved is the slice this reads.  The skip is that lifecycle's own, and
     it is load-bearing: on the first render the slice is still the empty one the
     reducer starts from and xAxis[0] would throw. */
  const mounted = useRef(false);
  useEffect(() => {
    if (!mounted.current) { mounted.current = true; return; }
    const next = Tools.copyOption(oldOrNew);
    setTimeList(next.xAxis[0].data);
    setNew1List(next.xAxis[0].num1New);
    setOld1List(next.xAxis[0].num1Old);
    setNew2List(next.xAxis[0].num2New);
    setOld2List(next.xAxis[0].num2Old);
    setPercent1List(next.series[0].data);
    setPercent2List(next.series[1].data);
    if (next.series[0].data && next.series[0].data && next.series[0].data[0]) {
                // next.legend.data.push(time1.current,time2.current);
                // next.series[0].name = time1.current;
                // next.series[1].name = time2.current;
      next.legend.data.push('时间一', '时间二');
      next.series[0].name = '时间一';
      next.series[1].name = '时间二';
      compareOldOrNewChart.current.setOption(next);
      compareOldOrNewChart.current.hideLoading();
    }
  }, [oldOrNew]);

  const changeTime = useCallback((e) => {
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
  }, []);
  const search = useCallback(() => {
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
    if (ms1 > ms2) { // 时间1大于时间2，错误
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
    loadOldOrNew({time: `${searchTime1},${searchTime2}`, range: selectTime});
    time1.current = searchTime1;
    time2.current = searchTime2;
  }, [loadOldOrNew, selectTime]);

  const rows = [];
  let rowTime1,
    rowTime2;
  if (timeList) {
    timeList.forEach((item, i) => {
      rowTime1 = timeList[i].split('/')[0];
      rowTime2 = timeList[i].split('/')[1];
      rows.push(<tr key={i}><td>{rowTime1}</td><td>{new1List[i]}</td><td>{old1List[i]}</td><td>{percent1List[i]}%</td><td>{rowTime2}</td><td>{new2List[i]}</td><td>{old2List[i]}</td><td>{percent2List[i]}%</td></tr>);
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
      <div className="panelHead">新老顾客对比&nbsp;<FaQuestion className="questionMark" />
        <div className="messageMark"><p>展示商城在两个时间段内新老顾客对比<br /></p></div></div>
      <div className="panelBody">
        <div ref={chartEl} className="compareOldOrNewChart" />
      </div>
    </div>
    <div className="panel">
      <div className="panelHead">新老顾客对比明细</div>
      <div className="panelBody">
        <table className="Table">
          <thead>
            <tr><th>时间一</th><th>新顾客量</th><th>老顾客量</th><th>新顾客率</th><th>时间二</th><th>新顾客量</th><th>老顾客量</th><th>新顾客率</th></tr>
          </thead>
          <tbody>
            {rows}
          </tbody>
        </table>
      </div>
    </div>
  </div>);
}

export default OldOrNew;
