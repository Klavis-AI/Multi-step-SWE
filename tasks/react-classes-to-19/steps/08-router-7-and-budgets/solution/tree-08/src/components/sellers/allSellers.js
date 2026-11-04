import React, {useEffect, useRef, useState} from 'react';
import {useSelector} from 'react-redux';
import echarts from 'echarts/lib/echarts';
import { Link } from 'react-router';
import Calendar from '../calendar';
import Tools from '../tools';
import '../../styles/calendar.scss';
import 'echarts/lib/chart/line';
import 'echarts/lib/chart/bar';
import 'echarts/lib/component/title';
import 'echarts/lib/component/legend';
import 'echarts/lib/component/tooltip';
import 'echarts/lib/component/toolbox';
import 'echarts/lib/component/grid';
import { FaAngleRight, FaAngleLeft, FaQuestion } from 'react-icons/fa';
import {useLazyAllSellersLineChartQuery} from '../../store/api';

function Chart() {
  const lineAndBar = useSelector(state => state.sellers.lineAndBar);

  /* Every one of the four fetches below reads the page number out of a ref, so
     none of them can be expressed as a subscription to a value this component
     renders from: each is triggered where the thunk used to go out.  The
     trigger is stable across renders. */
  const [loadLineChart] = useLazyAllSellersLineChartQuery();

  const chartEl = useRef(null);
  const chart = useRef(null);  // 全部商家图表
  const rightBtn = useRef(null);
  const selectTime1 = useRef(null);
  const selectTime2 = useRef(null);
  const resizeHandler = useRef(null);
  /* chartPage and lastPage were assigned straight onto the state object - by the
     paging buttons and by componentWillUpdate - and never read by render, so
     they were instance fields wearing state's clothes.  The only setState that
     touched chartPage put it back to 1 alongside a time change, which a ref does
     just as well without asking for a render nobody needed. */
  const chartPage = useRef(1);       // 按页分开商家数据，第一页
  const lastPage = useRef(false);
  const [selectTime, setSelectTime] = useState('day');
  const [time, setTime] = useState('day');

  const resizeFun = () => {
    if (resizeHandler.current) {
      clearTimeout(resizeHandler.current);
    }
    if (chart.current) {
      resizeHandler.current = setTimeout(() => {
        chart.current.resize();
      }, 100);
    }
  };

  /* componentWillMount asked for the first page, and componentWillUpdate's first
     half asked again on every later time: 点击day、week、month等请求，或点击查询.
     Comparing the old time with the incoming one is what a dependency list is,
     and its first run is the mount request, so the two are one effect. The page
     number is deliberately not in the list - the paging buttons ask for their
     page themselves, and a time change resets it before setting the time. */
  useEffect(() => {
      // console.log('componentWillMount')
        // dispatch(sellersAction.allSellersTableInit(time));
    loadLineChart({time, chartPage: chartPage.current});
  }, [time, loadLineChart]);

  // componentDidMount set the chart up, and componentWillUnmount is its cleanup.
  useEffect(() => {
    chart.current = echarts.init(chartEl.current);
    chart.current.showLoading();

    window.addEventListener('resize', resizeFun);
    return () => {
        // console.log('componentWillUnmount');
      chart.current.dispose();
      window.removeEventListener('resize', resizeFun);
    };
  }, []);

  /* The rest of componentWillUpdate.  It ran before every render, but pushing
     the same option and re-reading the same lastPage flag changed nothing on the
     renders it did not cause itself - so the renders that mattered are exactly
     the ones a new lineAndBar brings, and that is this effect's dependency.  The
     copy is echarts' to keep; 商家排名明细 below reads the store. */
  const mounted = useRef(false);
  useEffect(() => {
    if (!mounted.current) { mounted.current = true; return; }
        // console.log('..componentWillUpdate');
    const next = Tools.copyOption(lineAndBar);
    chart.current.setOption(next);
    chart.current.hideLoading();
        /* 判断商家排名的下一页，是否到了最后一页，是则将按钮设置为disabled*/
        // debugger
    if (next.xAxis[0].lastPage) {
            // debugger
      lastPage.current = true;
      rightBtn.current.style.backgroundColor = '#fff';   // 可改变按钮颜色，还没写
            // rightBtn.disabled=true;
            // chartPage.current-=1;
    } else {
            // rightBtn.disabled=false;background-color:#eee;
      rightBtn.current.style.backgroundColor = '#eee';
      lastPage.current = false;
    }
  }, [lineAndBar]);
    // componentDidUpdate(){
        // console.log('..componentDidUpdate')
        // debugger
        /* let lineAndBarObj = lineAndBar.toJS();
        chart.current.setOption(lineAndBarObj);
        chart.current.hideLoading()*/

        /* 判断商家排名的下一页，是否到了最后一页，是则将按钮设置为disabled*/
        /* if(lineAndBarObj.xAxis[0].lastPage){
            lastPage.current =  true;
            // rightBtn.style.backgroundColor='black';   //可改变按钮颜色，还没写
            // rightBtn.disabled=true;
            // chartPage.current-=1;
        }else{
            // rightBtn.disabled=false;
            lastPage.current =  false;
        }*/
    // }
    // componentWillReceiveProps(){
    // }
  const changeTime = (e) => {
    if (e.target.className === 'active') {
      return;
    }
    switch (e.target.innerText) {
      case '昨天':
        chartPage.current = 1;
        setTime('day');
        setSelectTime('day');
        return;
      case '最近一周':
        chartPage.current = 1;
        setTime('week');
        setSelectTime('week');
        return;
      case '最近一月':
        chartPage.current = 1;
        setTime('month');
        setSelectTime('month');
        return;
      case '最近一年':
        chartPage.current = 1;
        setTime('year');
        setSelectTime('year');
        return;
      case '全部':
        chartPage.current = 1;
        setTime('more');
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

    chartPage.current = 1;
    setTime(`${time1},${time2}`);
    setSelectTime('');
  };
  const turnLeft = () => {
    if (chartPage.current <= 1) {
      return;
    }
    chartPage.current -= 1;
    loadLineChart({time, chartPage: chartPage.current});
  };
  const turnRight = () => {
    if (lastPage.current) {
      return;
    }
    chartPage.current += 1;
        // 获取下一页商家数据
    loadLineChart({time, chartPage: chartPage.current});
  };

  const rows = [];
        // 跳转到商家实时页面，不需要接口传递
  const sellersUrl = '/sellers/';
  if (lineAndBar.series && lineAndBar.series[0].data) {
    const sellerName = lineAndBar.xAxis[0].data;
    const sellerNum = lineAndBar.series[0].data;
    const sellerPer = lineAndBar.series[1].data;
    sellerName.forEach((item, index) => {
      rows.push(<tr key={index}><th>{index + 1}</th><td><Link to={sellersUrl + item}>{item}</Link></td><td>{sellerNum[index]}{sellerPer[index] > 0 ? <span className="up">&nbsp;↑</span> : <span className="down">&nbsp;↓</span>}</td><td className={sellerPer[index] > 0 ? 'up' : 'down'}>{sellerPer[index]}%</td></tr>);
    });
  }

  return (<div className="chartWrapper">
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

    <div className="panel">
      <div className="panelHead">入店量环比&nbsp;
                  <FaQuestion className="questionMark" />
        <div className="messageMark"><p>展示商城各商家一定时间的入店量及环比增幅<br /><strong>入店量</strong>：一定时间内进入商城的人数<br /><strong>环比增幅</strong>：指与历史同时期比较。环比增长速度=（本期数－上期数）/上期数*100% 反映本期比上期增长了多少 环比发展速度=本期数/上期数*100% 反映本期比上期增长多少</p></div></div>
      <div className="panelBody">
        <div className="allSellersLineChartBtn">
          <button onClick={turnLeft}><FaAngleLeft /></button>
          <button onClick={turnRight} ref={rightBtn}><FaAngleRight /></button>
        </div>
        <div ref={chartEl} className="allSellersLineChart" />
      </div>
    </div>
    <div className="panel">
      <div className="panelHead">商家排名明细</div>
      <div className="panelBody">
        <table className="Table">
          <thead>
            <tr><th>排名</th><th>商店名称</th><th>平均客流</th><th>环比增幅</th></tr>
          </thead>
          <tbody>
            {rows.reverse()}
          </tbody>
        </table>
      </div>
    </div>
  </div>);
}

export default Chart;

