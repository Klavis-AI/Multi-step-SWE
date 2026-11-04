import React, {useCallback, useEffect, useRef, useState} from 'react';
import {useSelector} from 'react-redux';
import { useOutletContext } from 'react-router-dom';
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

  // componentDidMount asked for the time it had just copied out of props, and
  // componentWillReceiveProps asked again whenever that prop changed.
  // The class fetched in componentDidMount and again in
  // componentWillReceiveProps whenever the time prop changed.  A query
  // keyed on that prop does both.
  useStatisticsCustomerNumInitQuery(time);

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

  // The rest of componentWillReceiveProps.  It only ever ran on an update, and
  // this must not run on mount either: coming back to a chart a second time, the
  // class put the loading mask up and left it there until the request it had
  // just re-sent came back and moved the store.  Skipping the first run keeps
  // that.
  const mounted = useRef(false);
  useEffect(() => {
    if (!mounted.current) {
      mounted.current = true;
      return;
    }
    const next = Tools.copyOption(customerNum);
    if (next.series && next.series[0] && next.series[0].data && next.series[0].data[0]) {
      setTimeList(next.xAxis[0].data);
      setNumList(next.series[0].data);
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
