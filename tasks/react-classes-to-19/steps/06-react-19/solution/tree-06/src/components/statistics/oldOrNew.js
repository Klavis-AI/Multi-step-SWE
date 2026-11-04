import React, {useCallback, useEffect, useRef, useState} from 'react';
import {useSelector} from 'react-redux';
import { useOutletContext } from 'react-router-dom';
import Calendar from '../calendar';
import Tools from '../tools';
import {useStatisticsOldOrNewInitQuery} from '../../store/api';
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

function OldOrNew() {
  const {time, stateDefault} = useOutletContext();
  const oldOrNew = useSelector(state => state.statistics.oldOrNew);

  const chartEl = useRef(null);
  const chart = useRef(null);
  const resizeHandler = useRef(null);
  const [timeList, setTimeList] = useState('');
  const [newNumList, setNewNumList] = useState('');
  const [oldNumList, setOldNumList] = useState('');

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
  useStatisticsOldOrNewInitQuery(time);

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
    const next = Tools.copyOption(oldOrNew);
    if (next.series && next.series[0] && next.series[0].data && next.series[0].data[0]) {
      setTimeList(next.xAxis[0].data);
      setNewNumList(next.series[0].data);
      setOldNumList(next.series[1].data);
      chart.current.setOption(next);
      chart.current.hideLoading();
    }
  }, [oldOrNew]);

  const rows = [];
  let percent = '';
  let total = 0;
  if (timeList) {
    timeList.forEach((item, i) => {
      	total = parseInt(newNumList[i]) + parseInt(oldNumList[i]);
      	if (!total) {
      		percent = 0;
      	} else {
      		percent = parseInt(parseInt(newNumList[i]) / total * 100);
      	}
      if (!(i % tableSpace)) {
        rows.push(<tr key={i}><td>{timeList[i]}</td><td>{newNumList[i]}</td><td>{oldNumList[i]}</td><td>{percent}%</td></tr>);
      }
    });
  }
  return	(<div>
    <div className="panel">
      <div className="panelHead">新老顾客&nbsp;<FaQuestion className="questionMark" />
        <div className="messageMark"><p>展示商城在一定时间内的新老顾客的数量<br /><strong>新顾客</strong>：第一次进入店铺的顾客<strong>新顾客</strong>：不是第一次进入店铺的顾客</p></div></div>
      <div className="panelBody">
        <div className="statisticsOldOrNewChart" ref={chartEl} />
      </div>
    </div>
    <div className="panel">
      <div className="panelHead">新老顾客明细</div>
      <div className="panelBody">
        <table className="Table">
          <thead>
            <tr><th>时间</th><th>新顾客数量</th><th>老顾客数量</th><th>新顾客率</th></tr>
          </thead>
          <tbody>
            {rows}
          </tbody>
        </table>
      </div>
    </div>
  </div>);
}


export default  OldOrNew;
