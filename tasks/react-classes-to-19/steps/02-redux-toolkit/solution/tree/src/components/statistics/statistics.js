import React, {useCallback, useEffect, useRef, useState} from 'react';
// import {Table } from 'react-bootstrap';
import Calendar from '../calendar';
import Tools from '../tools';
// import lineAction from '../actions/lineAction';
// import FirstPageLineReducer from '../reducers/firstPageLineReducer';
import { Link } from 'react-router';
import FaAngleDoubleRight from 'react-icons/lib/fa/angle-double-right';
// var echarts = require('echarts/lib/echarts'); //必须
// require('echarts/lib/chart/pie'); //图表类型
// require('echarts/lib/component/title'); //标题插件


function SidebarNav() {
  return (<div id="sidebar_nav">
    <ul>
      <li><Link to='statistics/index' activeClassName="active" draggable="false">实时客流<FaAngleDoubleRight className="fa-angle-double-right" /></Link></li>
      <li><Link to="statistics/total" activeClassName="active" draggable="false">入店量 <FaAngleDoubleRight className="fa-angle-double-right" /></Link></li>
      <li><Link to="statistics/avg" activeClassName="active" draggable="false">客流量 <FaAngleDoubleRight className="fa-angle-double-right" /></Link></li>
      <li><Link to="statistics/peak" activeClassName="active" draggable="false">客流峰值 <FaAngleDoubleRight className="fa-angle-double-right" /></Link></li>
      <li><Link to="statistics/oldOrNew" activeClassName="active" draggable="false">新老顾客<FaAngleDoubleRight className="fa-angle-double-right" /></Link></li>
      <li><Link to="statistics/stay" activeClassName="active" draggable="false">停留时长<FaAngleDoubleRight className="fa-angle-double-right" /></Link></li>
      <li><Link to="statistics/deep" activeClassName="active" draggable="false">深访率<FaAngleDoubleRight className="fa-angle-double-right" /></Link></li>
      <li><Link to="statistics/out" activeClassName="active" draggable="false">跳出率<FaAngleDoubleRight className="fa-angle-double-right" /></Link></li>
      <li><Link to="statistics/active" activeClassName="active" draggable="false">顾客活跃度<FaAngleDoubleRight className="fa-angle-double-right" /></Link></li>
      <li><Link to="statistics/timeSection" activeClassName="active" draggable="false">各时间段人数<FaAngleDoubleRight className="fa-angle-double-right" /></Link></li>
      <li><Link to="statistics/cycle" activeClassName="active" draggable="false">来访周期<FaAngleDoubleRight className="fa-angle-double-right" /></Link></li>
    </ul>
  </div>);
}

function Statistics({children}) {
  const [selectTime, setSelectTime] = useState('day');
  const [time, setTime] = useState('day');

  const selectTime1 = useRef(null);
  const selectTime2 = useRef(null);

  /* index was state, but the only thing that ever wrote it was the same reading
     of location.href run in componentWillMount and again in every
     componentWillReceiveProps - that is, on the first render and on every render
     that follows a navigation.  Nothing else can move the URL without re-rendering
     this route, so reading it here is the same answer at the same moments. */
  const href = location.href;
  const index = href.indexOf('statistics/index') != -1;

  const stateDefault = useCallback(() => {
    setSelectTime('day');
    setTime('day');
  }, []);

  useEffect(() => {
    scrollTo(0, 0);
  }, []);

  /* componentWillReceiveProps' first half.  Its guard was the index of the route
     being left rather than the one being entered, which only differs on the two
     transitions that touch the index route: leaving it, the four nodes have just
     been mounted and carry no error class, so clearing it again writes nothing;
     entering it, the nodes it cleared were on their way out of the document.
     Either way the class this removes is one the search below had added. */
  useEffect(() => {
    if (!index) {
        // 选择时间去掉红色框ClassName
      const time1 = selectTime1.current.getElementsByClassName('calendar')[0].getElementsByTagName('input')[0].value;
      const time2 = selectTime2.current.getElementsByClassName('calendar')[0].getElementsByTagName('input')[0].value;
      selectTime1.current.className = selectTime1.current.className.replace(' selectTimeError', '');
      selectTime2.current.className = selectTime1.current.className.replace(' selectTimeError', '');
    }
  }, [href]);

  const changeTime = useCallback((e) => {
      // 选择时间去掉红色框ClassName
    const time1 = selectTime1.current.getElementsByClassName('calendar')[0].getElementsByTagName('input')[0].value;
    const time2 = selectTime2.current.getElementsByClassName('calendar')[0].getElementsByTagName('input')[0].value;
    selectTime1.current.className = selectTime1.current.className.replace(' selectTimeError', '');
    selectTime2.current.className = selectTime1.current.className.replace(' selectTimeError', '');

    if (e.target.className === 'active') {
      return;
    }
    switch (e.target.innerText) {
      case '昨天':
        setTime('day');
        setSelectTime('day');
        return;
      case '最近一周':
        setTime('week');
        setSelectTime('week');
        return;
      case '最近一月':
        setTime('month');
        setSelectTime('month');
        return;
      case '最近一年':
        setTime('year');
        setSelectTime('year');
        return;
      case '全部':
        setTime('more');
        setSelectTime('more');

    }
  }, []);
  const search = useCallback(() => {
    let time1 = selectTime1.current.getElementsByClassName('calendar')[0].getElementsByTagName('input')[0].value;
    let time2 = selectTime2.current.getElementsByClassName('calendar')[0].getElementsByTagName('input')[0].value;
    time1 = Tools.changeTime(time1);
    time2 = Tools.changeTime(time2);
        // 去除红色警示框ClassName
    selectTime1.current.className = selectTime1.current.className.replace(' selectTimeError', '');
    selectTime2.current.className = selectTime1.current.className.replace(' selectTimeError', '');
    const ms1 = new Date(time1).getTime();
    const ms2 = new Date(time2).getTime();
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

    setTime(`${time1},${time2}`);
    setSelectTime('');
  }, []);

  return (<div id="container">
    <SidebarNav />
    {!index && <div className="chartWrapper">
      <div className="selectOption inline">
                  快速选择：
                  <div className="quickSelect">
                    <ul>
                      <li><a className={selectTime == 'day' ? 'active' : ''} onClick={changeTime}>昨天</a></li>
                      <li><a className={selectTime == 'week' ? 'active' : ''} onClick={changeTime}>最近一周</a></li>
                      <li><a className={selectTime == 'month' ? 'active' : ''} onClick={changeTime}>最近一月</a></li>
                      <li><a className={selectTime == 'year' ? 'active' : ''} onClick={changeTime}>最近一年</a></li>
                      <li><a className={selectTime == 'more' ? 'active' : ''} onClick={changeTime}>全部</a></li>
                    </ul>
                  </div>
      </div>
      <div className="selectOption inline">

              自定义时间选择：
              <div className="selectTime1" ref={selectTime1}><Calendar /></div>
              &nbsp;至&nbsp;
              <div className="selectTime1" ref={selectTime2}><Calendar /></div>

        <div className="selectClick">
          <input type="button" value="查询" onClick={search} />
        </div>
      </div>

      {children && React.cloneElement(children, {time: time, stateDefault: stateDefault})}
    </div>}
    {index &&
      <div>
        {children && React.cloneElement(children, {time: time, stateDefault: stateDefault})}
      </div>
    }
  </div>);
}
// let Statistics=connect(state=>state.toJS(),null)(_statistics);

export default Statistics;
