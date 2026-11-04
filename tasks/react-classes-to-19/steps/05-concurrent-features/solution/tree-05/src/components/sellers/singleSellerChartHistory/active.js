import React, {useCallback, useEffect, useRef, useState} from 'react';
import {useSelector} from 'react-redux';
import { useOutletContext, useParams } from 'react-router-dom';
import echarts from 'echarts/lib/echarts';
import 'echarts/lib/chart/line';
import 'echarts/lib/component/toolbox';
import Tools from '../../tools';
import {useSingleSellerActiveInitQuery} from '../../../store/api';
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
  const params = useParams();
  const {time} = useOutletContext();
  const active = useSelector(state => state.sellers.active);

  // this.state held three different kinds of thing.  The echarts instance and
  // the debounce handle were assigned to and never read while rendering, so
  // they are refs; the two lists drive the table, so they are state; and the
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

  // componentDidMount fetched with the time it had just copied out of props,
  // and componentWillReceiveProps fetched again whenever that prop changed.
  // params.id is read but is not a dependency, because the class refetched on a
  // change of time alone: moving between two sellers' history pages without
  // touching the time picker did not refetch, and that is still true here.
  useSingleSellerActiveInitQuery({id: params.id, param: time});

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

  // The rest of componentWillReceiveProps.  It only ever ran on an update, and
  // this must not run on mount either: coming back to a chart a second time, the
  // class put the loading mask up and left it there until the request it had
  // just re-sent came back and moved the store.  Skipping the first run keeps
  // that.
  const mounted = useRef(false);
  useEffect(() => {
    if (!mounted.current) { mounted.current = true; return; }
    const next = Tools.copyOption(active);
    if (next.series && next.series[0] && next.series[0].data && next.series[0].data[0]) {
      setTimeList(next.xAxis[0].data.reverse());
      setNumList(next.series[0].data.reverse());
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
        <div className="messageMark"><p>展示在一定时间内店铺的活跃度<br /><strong>高活跃度</strong>：<br /><strong>中活跃度</strong>：<br /><strong>低活跃度</strong>：<br /><strong>沉睡活跃度</strong>：<br /></p></div></div>
      <div className="panelBody">
        <div className="singleSellerActiveChart" ref={chartEl} />
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
