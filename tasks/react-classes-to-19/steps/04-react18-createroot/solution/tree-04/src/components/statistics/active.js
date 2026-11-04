import React, {useCallback, useEffect, useRef, useState} from 'react';
import {useSelector} from 'react-redux';
import { useOutletContext } from 'react-router-dom';
import Calendar from '../calendar';
import Tools from '../tools';
import {useStatisticsActiveInitQuery} from '../../store/api';
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
import { FaQuestion } from 'react-icons/fa';

// The label the table repeats in its first column.  render() used to read the
// time out of this.state, which componentDidMount had assigned the prop to and
// componentWillReceiveProps kept in step with it, so the prop is the same value
// by the time there is a row to put it in.
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

function Active() {
  const {time, stateDefault} = useOutletContext();
  const active = useSelector(state => state.statistics.active);

  const chartEl = useRef(null);
  const chart = useRef(null);
  const resizeHandler = useRef(null);
  const [timeList, setTimeList] = useState('');
  const [numList, setNumList] = useState('');

  const resizeFun = useCallback(() => {
    if (resizeHandler.current) {
      clearTimeout(resizeHandler.current);
    }
    resizeHandler.current = setTimeout(() => {
      chart.current.resize();
    }, 100);
  }, []);

  // The class fetched in componentDidMount and again in
  // componentWillReceiveProps whenever the time prop changed.  A query
  // keyed on that prop does both.
  useStatisticsActiveInitQuery(time);

  useEffect(() => {
    chart.current = echarts.init(chartEl.current);
    chart.current.showLoading();
    window.addEventListener('resize', resizeFun);
    return () => {
      // 切换路由销毁echarts实例
      chart.current.dispose();
      stateDefault();
      window.removeEventListener('resize', resizeFun);
    };
  }, []);

  const mounted = useRef(false);
  useEffect(() => {
    if (!mounted.current) {
      mounted.current = true;
      return;
    }
    const next = Tools.copyOption(active);
    if (next.series && next.series[0] && next.series[0].data && next.series[0].data[0]) {
      setTimeList(next.xAxis[0].data);
      setNumList(next.series[0].data);
      chart.current.setOption(next);
      chart.current.hideLoading();
    }
  }, [active]);

  const rows = [];
  const label = timeLabelOf(time);
  if (timeList) {
    timeList.forEach((item, i) => {
      rows.push(<tr key={i}><td>{label}</td><td>{timeList[i]}</td><td>{numList[i]}</td></tr>);
    });
  }
  return	(<div>
    <div className="panel">
      <div className="panelHead">活跃度&nbsp;<FaQuestion className="questionMark" />
        <div className="messageMark"><p>展示商城在一定时间内顾客的活跃程度<br /><strong>高活跃度</strong>：<strong>中活跃度</strong>：<strong>低活跃度</strong>：<strong>沉睡活跃度</strong>：</p></div></div>
      <div className="panelBody">
        <div className="statisticsActiveChart" ref={chartEl} />
      </div>
    </div>
    <div className="panel">
      <div className="panelHead">活跃度明细</div>
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


export default Active;
