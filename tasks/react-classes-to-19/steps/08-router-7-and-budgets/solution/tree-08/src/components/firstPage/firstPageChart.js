

import React, {useEffect, useRef} from 'react';
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
import 'echarts/lib/component/legend';
import { FaQuestion } from 'react-icons/fa';
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

  /* The five values the screen shows were five useState that the effect below
     assigned on every round that carried data, which is a render's worth of
     work done a render late: each one is a read of the response the store
     already holds.  Render works them out where they are used, and the state
     they replace goes with them - a useState whose only writer was that effect
     was never state.
     chartTitle 也被存了起来，但从来没有被读过，只有一段注释提到它。 */
  const hasData = line.series[0].data && line.series[0].data[0];
  const chartNum = hasData ? line.series[0].data[line.series[0].data.length - 1] : '';     // 显示当前人数
  const chartTime = hasData ? line.xAxis.data[line.xAxis.data.length - 1] : '';     // 显示当前时间
  const yesterday = hasData ? line.xAxis.yesterday : '';     // 昨天的信息
  // 明细表是倒着排的.  reverse() rewrites the array it is called on, and this one
  // is the reducer's, so the table reverses a copy of its own.
  const timeList = hasData ? line.xAxis.data.slice().reverse() : '';
  const dataList = hasData ? line.series[0].data.slice().reverse() : '';

  const chartLine = useRef(null);
  const myChartLine = useRef(null);       // chart实例对象line
  const resizeHandler = useRef(null);

  const resizeFun = () => {
    if (resizeHandler.current) {
      clearTimeout(resizeHandler.current);
    }
    if (myChartLine.current) {
      resizeHandler.current = setTimeout(() => {
        myChartLine.current.resize();
                   // myChartPie.current.resize();
      }, 100);
    }
  };

  useEffect(() => {
        // console.log('componentWillMount');
        // debugger;

    myChartLine.current = echarts.init(chartLine.current);
    myChartLine.current.showLoading();

    window.addEventListener('resize', resizeFun);
    return () => {
            // console.log('componentWillUnmount')
      myChartLine.current.dispose();
      window.removeEventListener('resize', resizeFun);
    };
  }, []);

  // 定时刷新: the poll componentDidMount started and componentWillUnmount cleared.
  // Its one moving part is the trigger it calls, which is stable, so the
  // interval is built once and lives as long as the screen.
  useEffect(() => {
    const id = setInterval(() => {
      loadFetch();
    }, timerTime);
    return () => clearInterval(id);
  }, [loadFetch]);

    // componentDidUpdate(){
    //     // console.log('componentDidUpdate')
    // }
    // componentWillUpdate(nextProps,nextState){
    //     // console.log('componentWillUpdate')

    // }

  /* componentWillReceiveProps, which is only ever reached with a store slice
     connect has already decided is different from the last one - so "when line
     changes" is the same set of moments.  The skip is componentWillReceiveProps'
     own: it keeps the empty slice the reducer starts from out of the chart, and
     with it a hideLoading that would take the mask down before any data had
     arrived.

     The two reverse() calls stay where they were.  They mutate the copy after
     echarts has been handed it, exactly as they did before, and what echarts
     does with an array it is already holding is not a question this refactor
     gets to answer; the table takes its own copy instead. */
  const mounted = useRef(false);
  useEffect(() => {
    if (!mounted.current) { mounted.current = true; return; }
    const next = Tools.copyOption(line);
    if (next.xAxis.data) {
      myChartLine.current.setOption(next);
      myChartLine.current.hideLoading();
    }
    if (next.series[0].data && next.series[0].data[0]) {
      next.xAxis.data.reverse();
      next.series[0].data.reverse();
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
