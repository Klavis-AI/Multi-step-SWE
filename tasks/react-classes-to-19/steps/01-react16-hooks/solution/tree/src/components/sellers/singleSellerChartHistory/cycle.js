import React, {useCallback, useEffect, useRef, useState} from 'react';
import {useDispatch, useSelector} from 'react-redux';
import echarts from 'echarts/lib/echarts';
import 'echarts/lib/chart/line';
import sellersAction from '../../../actions/sellersAction';
import FaQuestion from 'react-icons/lib/fa/question';

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

function Cycle({params, time}) {
  const cycle = useSelector(state => state.getIn(['b', 'cycle']));
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

  // componentDidMount fetched with the time it had just copied out of props,
  // and componentWillReceiveProps fetched again whenever that prop changed.
  // params.id is read but is not a dependency, because the class refetched on a
  // change of time alone.
  useEffect(() => {
    dispatch(sellersAction.singleSellerCycleInit(params.id, time));
  }, [dispatch, time]);

  useEffect(() => {
    chart.current = echarts.init(chartEl.current);
    chart.current.showLoading();
    window.addEventListener('resize', resizeFun);
    return () => {
      // 切换路由销毁echarts实例
      chart.current.dispose();
      window.removeEventListener('resize', resizeFun);
    };
  }, []);

  // The rest of componentWillReceiveProps, which never ran on mount.  It must
  // not run on mount here either: on a revisit the slice still holds what the
  // last visit fetched and the repeated request is answered identically, so the
  // store never changes and the class left the loading mask up.
  const mounted = useRef(false);
  useEffect(() => {
    if (!mounted.current) { mounted.current = true; return; }
    const next = cycle.toJS();
    if (next.series && next.series[0] && next.series[0].data && next.series[0].data[0]) {
      setTimeList(next.xAxis[0].data);
      setNumList(next.series[0].data);
      chart.current.setOption(next);
      chart.current.hideLoading();
    }
  }, [cycle]);

  const rows = [];
  const label = timeLabelOf(time);
  if (timeList) {
    timeList.forEach((item, i) => {
      rows.push(<tr key={i}><td>{label}</td><td>{timeList[i]}</td><td>{numList[i]}</td></tr>);
    });
  }
  return (<div>
    <div className="panel">
      <div className="panelHead">来访周期&nbsp;<FaQuestion className="questionMark" />
        <div className="messageMark"><p>展示在一定时间内顾客距离上一次访问的时间<br /></p></div></div>
      <div className="panelBody">
        <div className="singleSellerCycleChart" ref={chartEl} />
      </div>
    </div>
    <div className="panel">
      <div className="panelHead">来访周期明细</div>
      <div className="panelBody">
        <table className="Table">
          <thead>
            <tr><th>时间范围</th><th>来访周期</th><th>人数</th></tr>
          </thead>
          <tbody>
            {rows}
          </tbody>
        </table>
      </div>
    </div>
  </div>);
}

export default  Cycle;
