

import React, {useCallback, useEffect, useRef, useState} from 'react';
import {useSelector} from 'react-redux';
import {useFirstPageNumInitQuery,
  useLazyFirstPageFetchQuery} from '../../store/api';
import echarts from 'echarts/lib/echarts';
import 'echarts/lib/chart/line';
import 'echarts/lib/chart/gauge';
import 'echarts/lib/component/title';
import 'echarts/lib/component/grid';
import 'echarts/lib/component/tooltip';
import 'echarts/lib/component/toolbox';
import 'echarts/lib/component/dataZoom';
import FaQuestion from 'react-icons/lib/fa/question';
import Tools from '../tools';

const tableSpace = 3;
const timerTime = 1000 * 10;

function Chart() {
  const line = useSelector(state => state.firstPage.line);

  /* 首页的折线图请求一次全量数据，然后每 10 秒只补一条最新的 - 两个不同的接口.
     The first is asked for once on mount and never again, which is a
     subscription; the second is only ever asked for by the interval below, so
     it is triggered from there rather than subscribed to. */
  useFirstPageNumInitQuery();
  const [loadFetch] = useLazyFirstPageFetchQuery();

  const [chartNum, setChartNum] = useState('');     // 显示当前人数
  const [chartTime, setChartTime] = useState('');     // 显示当前时间
  const [yesterday, setYesterday] = useState('');     // 昨天的信息
  const [timeList, setTimeList] = useState('');
  const [dataList, setDataList] = useState('');
    // chartTitle 也被存了起来，但从来没有被读过，只有一段注释提到它。

  const chartLine = useRef(null);
  const myChartLine = useRef(null);       // chart实例对象line
  const timer = useRef(null);           // 定时器
  const resizeHandler = useRef(null);

  const resizeFun = useCallback(() => {
    if (resizeHandler.current) {
      clearTimeout(resizeHandler.current);
    }
    if (myChartLine.current) {
      resizeHandler.current = setTimeout(() => {
        myChartLine.current.resize();
                   // myChartPie.current.resize();
      }, 100);
    }
  }, []);
  const fetchData = useCallback(() => {
    loadFetch();
  }, [loadFetch]);

  useEffect(() => {
        // console.log('componentWillMount');
        // debugger;

    myChartLine.current = echarts.init(chartLine.current);
    myChartLine.current.showLoading();

        // 定时刷新
    timer.current = setInterval(fetchData, timerTime);
    window.addEventListener('resize', resizeFun);
    return () => {
            // console.log('componentWillUnmount')
      myChartLine.current.dispose();
      clearInterval(timer.current);
      window.removeEventListener('resize', resizeFun);
    };
  }, []);

    // componentDidUpdate(){
    //     // console.log('componentDidUpdate')
    // }
    // componentWillUpdate(nextProps,nextState){
    //     // console.log('componentWillUpdate')

    // }

  /* componentWillReceiveProps, which is only ever reached with a store slice
     connect has already decided is different from the last one - so "when line
     changes" is the same set of moments.  The skip is componentWillReceiveProps'
     own and it is load-bearing here: on the first render line is still the empty
     slice the reducer starts from, and line.series[0].data would throw.

     The two reverse() calls mutate the copy after it has been handed to echarts,
     exactly as they did before; setOption has to stay ahead of them. */
  const mounted = useRef(false);
  useEffect(() => {
    if (!mounted.current) { mounted.current = true; return; }
    const next = Tools.copyOption(line);
    if (next.xAxis.data) {
      myChartLine.current.setOption(next);
      myChartLine.current.hideLoading();
    }
    if (next.series[0].data && next.series[0].data[0]) {
      setChartNum(next.series[0].data[next.series[0].data.length - 1]);
      setChartTime(next.xAxis.data[next.xAxis.data.length - 1]);
      setYesterday(next.xAxis.yesterday);
      setTimeList(next.xAxis.data.reverse());
      setDataList(next.series[0].data.reverse());
    }
  }, [line]);

  const rows = [];
  if (timeList) {
    timeList.forEach((item, i) => {
      if (!(i % tableSpace)) {
        rows.push(<tr key={i}><td>{timeList[i]}</td><td>{dataList[i]}</td></tr>);
      }
    });
  }
  return (<div className="chartWrapper firstWrapper">
    {/* <div className='chartMessage'>
              <p>当前人数：{chartNum}</p><p>昨日平均客流：2132</p><p>昨日高峰时段：16:30-21:00</p>
          </div>*/}
    <div className="topMessage">
      <div className="message message1"><div>
        <p>当前客流：{chartNum}                                                                                                               {/* yesterday.get('increase')+''=='true'?<span className='up'>&nbsp;↑</span> :<span className='down'>&nbsp;↓</span>*/} </p>
        <p>当前时间：{chartTime}</p>
      </div></div>
      <div className="message message2"><div>
        <p>昨日高峰客流：{yesterday.num}</p>
        {/* <p>昨日平均客流：{}</p>*/}
      </div></div>
      <div className="message message3"><div>
        <p>昨日高峰时间：{yesterday.countDate}</p>
      </div></div>
    </div>


    <div className="panel">
      <div className="panelHead">当前实时客流&nbsp;
              <FaQuestion className="questionMark" />
        <div className="messageMark"><p>展示商城实时客流量<br />实时信息将自动更新，更新间隔为：10秒钟</p></div>
      </div>
      <div className="panelBody">
        <div ref={chartLine} className="chartLine" />
      </div>
    </div>
    <div className="panel">
      <div className="panelHead">实时客流明细</div>
      <div className="panelBody">
        <table className=/* firstTable*/"Table">
          <thead>
            <tr><th>时间</th><th>客流量</th></tr>
          </thead>
          <tbody>
            {rows}
          </tbody>
        </table>
      </div>
    </div>
  </div>);
}


// connect(state => ({
//   stuff: state.get('stuff'),
//   otherStuff: state.get('otherStuff')
// })

export default Chart;
