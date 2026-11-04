import React, {useCallback, useEffect, useRef, useState} from 'react';
import {useSelector} from 'react-redux';
import { useOutletContext, useParams } from 'react-router-dom';
import echarts from 'echarts/lib/echarts';
import 'echarts/lib/chart/line';
import Tools from '../../tools';
import {useSingleSellerTimeSectionQuery} from '../../../store/api';
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

function StayBar() {
  const params = useParams();
  const {time} = useOutletContext();
  const timeSection = useSelector(state => state.sellers.timeSection);

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
  useSingleSellerTimeSectionQuery({id: params.id, param: time});

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
  // not run on mount here either: on a revisit the chart is a fresh instance
  // showing its loading mask, and the class left it there until the request it
  // had just re-sent came back and moved the store.
  const mounted = useRef(false);
  useEffect(() => {
    if (!mounted.current) { mounted.current = true; return; }
    const next = Tools.copyOption(timeSection);
    if (next.series[0] && next.series[0].data && next.series[0].data[0]) {
      setTimeList(next.xAxis[0].data);
      setNumList(next.series[0].data);
      chart.current.setOption(next);
      chart.current.hideLoading();
    }
  }, [timeSection]);

  const rows = [];
  const label = timeLabelOf(time);
  if (timeList) {
    timeList.forEach((item, i) => {
      rows.push(<tr key={i}><td>{label}</td><td>{timeList[i]}</td><td>{numList[i]}</td></tr>);
    });
  }
  return (<div>
    <div className="panel">
      <div className="panelHead">各时间段人数&nbsp;<FaQuestion className="questionMark" />
        <div className="messageMark"><p>展示在一定时间段中，一天某时间段内的人数的平均数</p></div></div>
      <div className="panelBody">
        <div className="singleSellerTimeSectionChart" ref={chartEl} />
      </div>
    </div>
    <div className="panel">
      <div className="panelHead">各时间段人数明细</div>
      <div className="panelBody">
        <table className="Table">
          <thead>
            <tr><th>时间范围</th><th>时间段</th><th>人数</th></tr>
          </thead>
          <tbody>
            {rows}
          </tbody>
        </table>
      </div>
    </div>
  </div>);
}

// The store slice and the bound action creator the class validated are no
// longer props - they are read with useSelector and useDispatch - so what is
// left to validate is what the route and the parent still hand in.

export default StayBar;
