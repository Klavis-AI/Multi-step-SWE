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

function Cycle({time, stateDefault}) {
  const cycle = useSelector(state => state.getIn(['c', 'cycle']));
  const dispatch = useDispatch();

  const chartEl = useRef(null);
  const chart = useRef(null);
  const resizeHandler = useRef(null);
  const [timeList, setTimeList] = useState('');
  const [numList, setNumList] = useState('');
  const [totalNum, setTotalNum] = useState('');

  const resizeFun = useCallback(() => {
    if (resizeHandler.current) {
      clearTimeout(resizeHandler.current);
    }
    resizeHandler.current = setTimeout(() => {
      chart.current.resize();
    }, 100);
  }, []);

  useEffect(() => {
    dispatch(statisticsAction.statisticsCycleInit(time));
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
    const next = cycle.toJS();
    if (next.series && next.series[0] && next.series[0].data && next.series[0].data[0]) {
      const nums = next.series[0].data;
      setTimeList(next.xAxis[0].data);
      setNumList(nums);
      setTotalNum(nums.reduce((x, y) => (parseInt(x) + parseInt(y))));
      chart.current.setOption(next);
      chart.current.hideLoading();
    }
  }, [cycle]);

  const rows = [];
  const label = timeLabelOf(time);
  let percent = '';
  if (timeList) {
    timeList.forEach((item, i) => {
      if (!totalNum) {
      	percent = 0;
      } else {
  			percent = parseInt((parseInt(numList[i]) / parseInt(totalNum)) * 100);
      }
      rows.push(<tr key={i}><td>{label}</td><td>{timeList[i]}</td><td>{numList[i]}</td><td>{percent}%</td></tr>);
    });
  }
  return	(<div>
    <div className="panel">
      <div className="panelHead">来访周期&nbsp;<FaQuestion className="questionMark" />
        <div className="messageMark"><p>展示商城在一定时间内顾客距离上次来访的时间<br /></p></div></div>
      <div className="panelBody">
        <div className="statisticsCycleChart" ref={chartEl} />
      </div>
    </div>
    <div className="panel">
      <div className="panelHead">来访周期明细</div>
      <div className="panelBody">
        <table className="Table">
          <thead>
            <tr><th>时间范围</th><th>来访周期</th><th>人数</th><th>所占比例</th></tr>
          </thead>
          <tbody>
            {rows}
          </tbody>
        </table>
      </div>
    </div>
  </div>);
}


export default Cycle;
