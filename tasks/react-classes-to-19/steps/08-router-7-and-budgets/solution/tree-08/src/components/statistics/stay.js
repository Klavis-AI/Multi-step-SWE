import React, {useEffect, useRef} from 'react';
import {useSelector} from 'react-redux';
import { useOutletContext } from 'react-router';
import Calendar from '../calendar';
import Tools from '../tools';
import {useStatisticsStayInitQuery} from '../../store/api';
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

function timeLabelOf(time) {
  switch (time) {
    case 'day':return '最近一天';
    case 'week':return '最近一周';
    case 'month':return '最近一月';
    case 'year':return '最近一年';
    case 'more':return '开店以来';
    default:return time.split(',').join(' 至 ');
  }
}

function Stay() {
  const {time, stateDefault} = useOutletContext();
  const stay = useSelector(state => state.statistics.stay);

  const chartEl = useRef(null);
  const chart = useRef(null);
  const resizeHandler = useRef(null);

  /* The table's values were 2 useState that the effect below assigned every
     time the slice changed, which is a render's worth of work done a render
     late: each one is a read of the response the store already holds.  Render
     works them out where they are used, and the state they replace goes with
     them - a useState whose only writer was that effect was never state. */
  const hasData = stay.series && stay.series[0] && stay.series[0].data && stay.series[0].data[0];
  const timeList = hasData ? stay.xAxis[0].data : '';
  const numList = hasData ? stay.series[0].data : '';

  // The class fetched in componentDidMount and again in
  // componentWillReceiveProps whenever the time prop changed.  A query
  // keyed on that prop does both.
  useStatisticsStayInitQuery(time);

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

  /* componentWillReceiveProps, which only ever did anything when this slice
     moved.  What is left of it is the chart; the mount skip went out with the
     state assignments, because the guard it was there to protect is the one
     still standing below, and on the first render that guard is simply false. */
  useEffect(() => {
    const next = Tools.copyOption(stay);
    if (next.series && next.series[0] && next.series[0].data && next.series[0].data[0]) {
      chart.current.setOption(next);
      chart.current.hideLoading();
    }
  }, [stay]);

  const rows = [];
  const label = timeLabelOf(time);
  if (timeList) {
    timeList.forEach((item, i) => {
      rows.push(<tr key={i}><td>{label}</td><td>{timeList[i]}</td><td>{numList[i]}</td></tr>);
    });
  }
  return	(<div>
    <div className="panel">
      <div className="panelHead">停留时长&nbsp;<FaQuestion className="questionMark" />
        <div className="messageMark"><p>展示商城在一定时间内的顾客的停留时长<br /></p></div></div>
      <div className="panelBody">
        <div className="statisticsStayChart" ref={chartEl} />
      </div>
    </div>
    <div className="panel">
      <div className="panelHead">停留时长明细</div>
      <div className="panelBody">
        <table className="Table">
          <thead>
            <tr><th>时间范围</th><th>活跃度</th><th>人数</th></tr>
          </thead>
          <tbody>
            {rows}
          </tbody>
        </table>
      </div>
    </div>
  </div>);
}


export default Stay;
