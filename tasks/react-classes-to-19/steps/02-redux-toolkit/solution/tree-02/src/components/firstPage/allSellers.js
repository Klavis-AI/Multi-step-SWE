

import React, {useCallback, useEffect, useRef} from 'react';
import {useSelector} from 'react-redux';
import Tools from '../tools';
import {useFirstPageSellersInitQuery} from '../../store/api';
import echarts from 'echarts/lib/echarts';
import 'echarts/lib/chart/line';
import 'echarts/lib/chart/gauge';
import 'echarts/lib/component/title';
import 'echarts/lib/component/grid';
import 'echarts/lib/component/tooltip';
import 'echarts/lib/component/dataZoom';

const timerTime = 1000 * 5;

/* The constructor also carried myChartPie, param, chartNum, chartTime,
   chartTitle, yesterday and test.  Nothing ever assigned to any of them and
   nothing but a comment ever read one, so there is nothing for them to become. */
function Chart() {
  const sellers = useSelector(state => state.firstPage.sellers);

  /* componentWillMount asked for this data and componentDidMount started a 5s
     interval that asked for exactly the same thing again - a refresh of one
     request on a fixed interval, which is what a polling interval on a
     subscription is, so there is no interval left to clear.  The effect below
     has no dependency list and therefore runs after every commit, so nothing is
     selected out of the query: its loading transitions would otherwise add
     commits the class never had, and the data arrives through the slice above. */
  useFirstPageSellersInitQuery(undefined, {
    pollingInterval: timerTime,
    selectFromResult: () => ({})
  });

  const chartEl = useRef(null);
  const chart = useRef(null);       // chart实例对象line
  const resizeHandler = useRef(null);

  const resizeFun = useCallback(() => {
    if (resizeHandler.current) {
      clearTimeout(resizeHandler.current);
    }
    if (chart.current) {
      resizeHandler.current = setTimeout(() => {
        chart.current.resize();
                   // myChartPie.current.resize();
      }, 100);
    }
  }, []);

  // componentDidMount raised the mask, componentWillUnmount took it down.
  useEffect(() => {
    console.log('componentWillMount');
        // debugger;

        // let domPie = chartPie.current;
        // chart.current = echarts.init(dom);
        // dispatch(FrstPageAction.init(chart.current, param));
        // timer.current = setInterval(start, 2000);
    chart.current = echarts.init(chartEl.current);
    chart.current.showLoading();   // 显是遮罩

    window.addEventListener('resize', resizeFun);
    return () => {
      console.log('componentWillUnmount');
      chart.current.dispose();   // 销毁实例
      window.removeEventListener('resize', resizeFun);
    };
  }, []);

  // componentDidUpdate, and it really is that one and not componentWillReceiveProps:
  // it ran after every update, with no comparison against the previous props.  An
  // effect with no dependency list runs on exactly those commits, and the mount
  // skip is componentDidUpdate's own.  Nothing in here sets state, so re-running
  // on every commit cannot feed itself.
  const mounted = useRef(false);
  useEffect(() => {
    if (!mounted.current) { mounted.current = true; return; }
    const next = Tools.copyOption(sellers);
    if (next.series[0].data) {
      chart.current.setOption(next);
      chart.current.hideLoading();
            // debugger
            // myChartPie.current.setOption(pie.toJS());
            // myChartPie.current.hideLoading();
    }
    console.log('componentDidUpdate');
        // console.log(props)
  });

        // console.log('firstPageChart:'+test++ +',render,'+new Date().getTime())

        // let rows=
        // debugger

  return (<div className="chartWrapper">
    <div className="panel">
      <div className="panelHead">
        {/* {chartTitle}*/}</div>
      <div className="panelBody">
        <div ref={chartEl} className="firstPageSellers" />
        <p>
                      当前实时客流数据
                  </p>
        <table className="firstTable">
          <thead>
            <tr><th>时间</th><th>8:00</th><th>9:00</th><th>10:00</th><th>11:00</th><th>12:00</th><th>13:00</th><th>14:00</th><th>15:00</th><th>16:00</th></tr>
          </thead>
          <tbody>
            <tr><td>客流量</td><td>654</td><td>754</td><td>654</td><td>654</td><td>754</td><td>654</td><td>654</td><td>754</td><td>654</td></tr>
            <tr><td>其他实时数据</td><td>654</td><td>754</td><td>654</td><td>654</td><td>754</td><td>654</td><td>654</td><td>754</td><td>654</td></tr>

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
