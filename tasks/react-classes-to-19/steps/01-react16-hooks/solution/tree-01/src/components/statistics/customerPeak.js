import React, {useCallback, useEffect, useRef, useState} from 'react';
import {useDispatch, useSelector} from 'react-redux';
import Immutable from 'immutable';
import Calendar from '../calendar';
import statisticsAction from '../../actions/statisticsAction';
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
import FaQuestion from 'react-icons/lib/fa/question';

const tableSpace = 1;

function CustomerPeak({time, stateDefault}) {
  const customerPeak = useSelector(state => state.getIn(['c', 'customerPeak']));
  const dispatch = useDispatch();

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

  useEffect(() => {
    dispatch(statisticsAction.statisticsPeakInit(time));
  }, [dispatch, time]);

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
    const next = customerPeak.toJS();
    if (next.series[0] && next.series[0].data && next.series[0].data[0]) {
      setTimeList(next.xAxis[0].data);
      setNumList(next.series[0].data);
      chart.current.setOption(next);
      chart.current.hideLoading();
    }
  }, [customerPeak]);

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
      <div className="panelHead">客流量峰值&nbsp;<FaQuestion className="questionMark" />
        <div className="messageMark"><p>展示商城在一定时间内的客流峰值<br /></p></div></div>
      <div className="panelBody">
        <div className="statisticsPeakChart" ref={chartEl} />
      </div>
    </div>
    <div className="panel">
      <div className="panelHead">客流量峰值明细</div>
      <div className="panelBody">
        <table className="Table">
          <thead>
            <tr><th>时间</th><th>峰值人数</th></tr>
          </thead>
          <tbody>
            {rows}
          </tbody>
        </table>
      </div>
    </div>
  </div>);
}


export default CustomerPeak;
