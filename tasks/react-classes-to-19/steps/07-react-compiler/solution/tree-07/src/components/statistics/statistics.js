import React, {Suspense, useEffect, useRef, useState, useTransition} from 'react';
import ActiveLink from '../activeLink';
// import {Table } from 'react-bootstrap';
import Calendar from '../calendar';
import Tools from '../tools';
// import lineAction from '../actions/lineAction';
// import FirstPageLineReducer from '../reducers/firstPageLineReducer';
import { Outlet, useLocation } from 'react-router-dom';
import { FaAngleDoubleRight } from 'react-icons/fa';
// var echarts = require('echarts/lib/echarts'); //必须
// require('echarts/lib/chart/pie'); //图表类型
// require('echarts/lib/component/title'); //标题插件


function SidebarNav() {
  return (<div id="sidebar_nav">
    <ul>
      <li><ActiveLink to="/statistics/index" draggable="false">实时客流<FaAngleDoubleRight className="fa-angle-double-right" /></ActiveLink></li>
      <li><ActiveLink to="/statistics/total" draggable="false">入店量 <FaAngleDoubleRight className="fa-angle-double-right" /></ActiveLink></li>
      <li><ActiveLink to="/statistics/avg" draggable="false">客流量 <FaAngleDoubleRight className="fa-angle-double-right" /></ActiveLink></li>
      <li><ActiveLink to="/statistics/peak" draggable="false">客流峰值 <FaAngleDoubleRight className="fa-angle-double-right" /></ActiveLink></li>
      <li><ActiveLink to="/statistics/oldOrNew" draggable="false">新老顾客<FaAngleDoubleRight className="fa-angle-double-right" /></ActiveLink></li>
      <li><ActiveLink to="/statistics/stay" draggable="false">停留时长<FaAngleDoubleRight className="fa-angle-double-right" /></ActiveLink></li>
      <li><ActiveLink to="/statistics/deep" draggable="false">深访率<FaAngleDoubleRight className="fa-angle-double-right" /></ActiveLink></li>
      <li><ActiveLink to="/statistics/out" draggable="false">跳出率<FaAngleDoubleRight className="fa-angle-double-right" /></ActiveLink></li>
      <li><ActiveLink to="/statistics/active" draggable="false">顾客活跃度<FaAngleDoubleRight className="fa-angle-double-right" /></ActiveLink></li>
      <li><ActiveLink to="/statistics/timeSection" draggable="false">各时间段人数<FaAngleDoubleRight className="fa-angle-double-right" /></ActiveLink></li>
      <li><ActiveLink to="/statistics/cycle" draggable="false">来访周期<FaAngleDoubleRight className="fa-angle-double-right" /></ActiveLink></li>
    </ul>
  </div>);
}

function Statistics() {
  const [selectTime, setSelectTime] = useState('day');
  const [time, setTime] = useState('day');

  /* A range pick changes two things and only one of them is urgent.
     `selectTime` is which link is highlighted - the feedback the click is for -
     and `time` goes down the <Outlet>'s context to the panel below, where it
     re-renders a chart's worth of tree and starts a new request.  So the second
     one goes inside a transition and the first one does not: a transition around
     the highlight would make it wait for the chart. */
  const [, startRangeChange] = useTransition();
  const changeRange = (next) => {
    startRangeChange(() => {
      setTime(next);
    });
  };

  const selectTime1 = useRef(null);
  const selectTime2 = useRef(null);

  /* index was state, but the only thing that ever wrote it was the same reading
     of location.href run in componentWillMount and again in every
     componentWillReceiveProps - that is, on the first render and on every render
     that follows a navigation.  The address is read from the router rather than
     from window, because the router is what makes those renders happen: the old
     one handed this component a new children prop on every navigation, the new
     one renders the same element object each time and React would otherwise skip
     the re-render, leaving the answer at whatever it was on the first one. */
  const href = useLocation().pathname;
  const index = href.indexOf('statistics/index') != -1;

  const stateDefault = () => {
    setSelectTime('day');
    changeRange('day');
  };

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

  const changeTime = (e) => {
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
        changeRange('day');
        setSelectTime('day');
        return;
      case '最近一周':
        changeRange('week');
        setSelectTime('week');
        return;
      case '最近一月':
        changeRange('month');
        setSelectTime('month');
        return;
      case '最近一年':
        changeRange('year');
        setSelectTime('year');
        return;
      case '全部':
        changeRange('more');
        setSelectTime('more');

    }
  };
  const search = () => {
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

    changeRange(`${time1},${time2}`);
    setSelectTime('');
  };

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

      {/* Same seam as firstPage.js: the sidebar and the range strip above are
          the shell and stay put, and the chart body below is what is allowed to
          arrive late.  Both branches get one, because both render a panel and
          which branch runs is decided by the address. */}
      <Suspense fallback={<div className="panelLoading">加载中…</div>}>
        <Outlet context={{time: time, stateDefault: stateDefault}} />
      </Suspense>
    </div>}
    {index &&
      <div>
        <Suspense fallback={<div className="panelLoading">加载中…</div>}>
          <Outlet context={{time: time, stateDefault: stateDefault}} />
        </Suspense>
      </div>
    }
  </div>);
}
// let Statistics=connect(state=>state.toJS(),null)(_statistics);

export default Statistics;
