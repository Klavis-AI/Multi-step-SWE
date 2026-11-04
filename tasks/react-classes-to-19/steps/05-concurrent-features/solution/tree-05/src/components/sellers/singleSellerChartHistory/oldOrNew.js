import React, {useCallback, useEffect, useRef, useState} from 'react';
import {useSelector} from 'react-redux';
import { useOutletContext, useParams } from 'react-router-dom';
import echarts from 'echarts/lib/echarts';
import 'echarts/lib/chart/line';
import 'echarts/lib/component/toolbox';
import Tools from '../../tools';
import {useSingleSellerOldOrNewQuery} from '../../../store/api';
import { FaQuestion } from 'react-icons/fa';

function StayBar() {
  const params = useParams();
  const {time} = useOutletContext();
  const oldOrNew = useSelector(state => state.sellers.oldOrNew);

  const chartEl = useRef(null);
  const chart = useRef(null);
  const resizeHandler = useRef(null);
  const [timeList, setTimeList] = useState('');
  const [oldNumList, setOldNumList] = useState('');
  const [newNumList, setNewNumList] = useState('');

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
  useSingleSellerOldOrNewQuery({id: params.id, param: time});

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
      rows.push(<tr key={i}><td>{timeList[i]}</td><td>{newNumList[i]}</td><td>{oldNumList[i]}</td><td>{percent}%</td></tr>);
    });
  }
  return (<div>
    <div className="panel">
      <div className="panelHead">新老顾客&nbsp;<FaQuestion className="questionMark" />
        <div className="messageMark"><p>展示在一定时间内店铺的新老顾客数量<br /><strong>新顾客</strong>：第一次进入店铺的顾客<br /><strong>老顾客</strong>：不是第一次进入店铺的顾客<br /></p></div></div>
      <div className="panelBody">
        <div className="singleSellerOldOrNewChart" ref={chartEl} />
      </div>
    </div>
    <div className="panel">
      <div className="panelHead">新老顾客明细</div>
      <div className="panelBody">
        <table className="Table">
          <thead>
            <tr><th>时间</th><th>新顾客</th><th>老顾客</th><th>新顾客比例</th></tr>
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
