import React, {useEffect, useRef} from 'react';
import {useSelector} from 'react-redux';
import { useOutletContext } from 'react-router';
import Calendar from '../calendar';
import Tools from '../tools';
import {useStatisticsCustomerNumInitQuery} from '../../store/api';
import echarts from 'echarts/lib/echarts';
import 'echarts/lib/chart/line';
import 'echarts/lib/chart/radar';
import 'echarts/lib/chart/bar';
import 'echarts/lib/chart/pie';
import 'echarts/lib/component/grid';
import 'echarts/lib/component/title';
import 'echarts/lib/component/legend';
import 'echarts/lib/component/tooltip';
import 'echarts/lib/component/dataZoom';
import 'echarts/lib/component/toolbox';
import { FaQuestion } from 'react-icons/fa';

const tableSpace = 1;

function CustomerNum() {
  const {time, stateDefault} = useOutletContext();
  const customerNum = useSelector(state => state.statistics.customerNum);

  // this.state held three different kinds of thing.  The echarts instance and
  // the debounce timer were assigned to directly and never read while rendering,
  // so they are refs; the two lists drive the table, so they are state; and the
  // chart container, which used to be a string ref read back through
  // findDOMNode, is now a ref on the element itself.
  const chartEl = useRef(null);
  const chart = useRef(null);
  const resizeHandler = useRef(null);

  /* The table's values were 2 useState that the effect below assigned every
     time the slice changed, which is a render's worth of work done a render
     late: each one is a read of the response the store already holds.  Render
     works them out where they are used, and the state they replace goes with
     them - a useState whose only writer was that effect was never state. */
  const hasData = customerNum.series && customerNum.series[0] && customerNum.series[0].data && customerNum.series[0].data[0];
  const timeList = hasData ? customerNum.xAxis[0].data : '';
  const numList = hasData ? customerNum.series[0].data : '';

  // componentDidMount asked for the time it had just copied out of props, and
  // componentWillReceiveProps asked again whenever that prop changed.
  // The class fetched in componentDidMount and again in
  // componentWillReceiveProps whenever the time prop changed.  A query
  // keyed on that prop does both.
  useStatisticsCustomerNumInitQuery(time);

  /* componentWillUnmount put the range back to its default on the way out.
     The callback it calls is the one the outlet context is carrying when the
     panel goes away, which need not be the one it was carrying on the mount
     round, so a ref kept current by its own effect is what the teardown reads:
     naming the callback in the list below would let a new one tear the chart
     down and reset the range in the middle of the panel's life. */
  const stateDefaultRef = useRef(stateDefault);
  useEffect(() => {
    stateDefaultRef.current = stateDefault;
  });

  /* The chart itself, and the listener that keeps it the size of its box -
     both of them reaching for something that is not React, which is what an
     effect is for.  It reads nothing from the render, so the empty list is the
     honest one rather than a suppressed one. */
  useEffect(() => {
    chart.current = echarts.init(chartEl.current);
    chart.current.showLoading();
    const resizeFun = () => {
      if (resizeHandler.current) {
        clearTimeout(resizeHandler.current);
      }
      resizeHandler.current = setTimeout(() => {
        chart.current.resize();
      }, 100);
    };

    window.addEventListener('resize', resizeFun);
    return () => {
      // 切换路由销毁echarts实例
      chart.current.dispose();
      stateDefaultRef.current();
      window.removeEventListener('resize', resizeFun);
    };
  }, []);

  // The rest of componentWillReceiveProps.  It only ever ran on an update, and
  // this must not run on mount either: coming back to a chart a second time, the
  // class put the loading mask up and left it there until the request it had
  // just re-sent came back and moved the store.  Skipping the first run keeps
  // that.
  /* componentWillReceiveProps, which only ever did anything when this slice
     moved.  What is left of it is the chart; the mount skip went out with the
     state assignments, because the guard it was there to protect is the one
     still standing below, and on the first render that guard is simply false. */
  useEffect(() => {
    const next = Tools.copyOption(customerNum);
    if (next.series && next.series[0] && next.series[0].data && next.series[0].data[0]) {
      chart.current.setOption(next);
      chart.current.hideLoading();
    }
  }, [customerNum]);

  const rows = [];
  if (timeList) {
    timeList.forEach((item, i) => {
      if (!(i % tableSpace)) {
        rows.push(<tr key={i}><td>{timeList[i]}</td><td>{numList[i]}</td></tr>);
      }
    });
  }
  return	(<div>
    <div className="panel">
      <div className="panelHead">入店量&nbsp;<FaQuestion className="questionMark" />
        <div className="messageMark"><p>展示在一定时间内进入商城的人数<br /></p></div></div>
      <div className="panelBody">
        <div className="statisticsCustomerNumChart" ref={chartEl} />
      </div>
    </div>
    <div className="panel">
      <div className="panelHead">入店量明细</div>
      <div className="panelBody">
        <table className="Table">
          <thead>
            <tr><th>时间</th><th>人数</th></tr>
          </thead>
          <tbody>
            {rows}
          </tbody>
        </table>
      </div>
    </div>
  </div>);
}


export default CustomerNum;
