
import React, {useCallback, useEffect, useRef, useState} from 'react';
import {useDispatch, useSelector} from 'react-redux';
import echarts from 'echarts/lib/echarts';
import Calendar from '../../calendar';
import Tools from '../../tools';
import 'echarts/lib/chart/line';
import 'echarts/lib/chart/bar';
import 'echarts/lib/component/title';
import 'echarts/lib/component/legend';
import 'echarts/lib/component/tooltip';
import FaQuestion from 'react-icons/lib/fa/question';

import compareAction from '../../../actions/compareAction';

/* state.Data was built empty and never written again, so the table branch that
   reads it below has never run.  It stays a constant for the same reason it was
   never a useState: nothing can change it. */
const Data = [];

function CustomerAvg() {
  const customerAvg = useSelector(state => state.getIn(['d', 'customerAvg']));
  const dispatch = useDispatch();

  const [selectTime, setSelectTime] = useState('day');
  const [timeList, setTimeList] = useState('');
  const [num1List, setNum1List] = useState('');
  const [num2List, setNum2List] = useState('');

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

  const resizeFun = useCallback(() => {
    if (resizeHandler.current) {
      clearTimeout(resizeHandler.current);
    }
    if (compareCustomerAvgChart.current) {
      resizeHandler.current = setTimeout(() => {
        compareCustomerAvgChart.current.resize();
      }, 100);
    }
  }, []);

    // componentWillMount(){
        // console.log('componentWillMount')

 //        dispatch(compareAction.comparenumInit(time.current,chartPage));

    // }

  useEffect(() => {
        // console.log('componentDidMount');
        // 	//dispatch(compareAction.allSellersTableInit());
    const getTime = Tools.getTime();
    time.current = getTime;
    time1.current = getTime.split(',')[0];
    time2.current = getTime.split(',')[1];
    dispatch(compareAction.numAvgInit(Tools.changeTime(time.current), selectTime));   // 默认无参，可选

    const input1 = selectTime1.current.getElementsByClassName('calendar')[0].getElementsByTagName('input')[0];
    input1.value = time.current.split(',')[0];

    compareCustomerAvgChart.current = echarts.init(chartEl.current);
    compareCustomerAvgChart.current.showLoading();

    window.addEventListener('resize', resizeFun);
    return () => {
		// console.log('componentWillUnmount');
      compareCustomerAvgChart.current.dispose();
      window.removeEventListener('resize', resizeFun);
	// }
 //
    };
  }, []);

  /* componentWillReceiveProps, guarded on "the props object is a new one" - which
     under connect it always was, so the only thing that could actually have moved
     is the slice this reads.  The skip is that lifecycle's own, and it is
     load-bearing: on the first render the slice is still the empty one the reducer
     starts from and xAxis[0] would throw. */
  const mounted = useRef(false);
  useEffect(() => {
    if (!mounted.current) { mounted.current = true; return; }
    const next = customerAvg.toJS();  // 当props改变时才触发。只改变selectTime不触发
    setTimeList(next.xAxis[0].data);
    setNum1List(next.series[0].data);
    setNum2List(next.series[1].data);
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
  const changeTime = useCallback((e) => {
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
    dispatch(compareAction.numAvgInit(`${searchTime1},${searchTime2}`, selectTime));
    time1.current = searchTime1;
    time2.current = searchTime2;
  }, [dispatch, selectTime]);

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

