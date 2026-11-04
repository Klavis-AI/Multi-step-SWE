import React, {useEffect, useRef, useState} from 'react';
import {useSelector} from 'react-redux';
import { useOutletContext, useParams } from 'react-router-dom';
import echarts from 'echarts/lib/echarts';
import 'echarts/lib/chart/line';
import 'echarts/lib/component/toolbox';
import { FaQuestion } from 'react-icons/fa';

import Tools from '../../tools';
import {useSingleSellerDeepVisitQuery} from '../../../store/api';

function DeepVisit() {
  const params = useParams();
  const {time} = useOutletContext();
  const deepVisit = useSelector(state => state.sellers.deepVisit);

  const chartEl = useRef(null);
  const chart = useRef(null);
  const resizeHandler = useRef(null);
  const [timeList, setTimeList] = useState('');
  const [deepList, setDeepList] = useState('');
  const [deepPercentList, setDeepPercentList] = useState('');
  const [outList, setOutList] = useState('');
  const [outPercentList, setOutPercentList] = useState('');

  const resizeFun = () => {
    if (resizeHandler.current) {
      clearTimeout(resizeHandler.current);
    }
    resizeHandler.current = setTimeout(() => {
      chart.current.resize();
    }, 100);
  };

  // componentDidMount fetched with the time it had just copied out of props,
  // and componentWillReceiveProps fetched again whenever that prop changed.
  // params.id is read but is not a dependency, because the class refetched on a
  // change of time alone.
  useSingleSellerDeepVisitQuery({id: params.id, param: time});

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
    const deep = Tools.copyOption(deepVisit);
    if (deep.series && deep.series[0] && deep.series[0].data && deep.series[0].data[0]) {
      setTimeList(deep.xAxis.data);
      setDeepList(deep.xAxis.deepNum);
      setDeepPercentList(deep.series[0].data);
      setOutList(deep.xAxis.outNum);
      setOutPercentList(deep.series[1].data);
      chart.current.setOption(deep);
      chart.current.hideLoading();
    }
  }, [deepVisit]);

  const rows = [];
  if (timeList) {
    timeList.forEach((item, i) => {
      rows.push(<tr key={i}><td>{timeList[i]}</td><td>{deepList[i]}</td><td>{deepPercentList[i]}%</td><td>{outList[i]}</td><td>{outPercentList[i]}%</td></tr>);
    });
  }
  return (<div>
    <div className="panel">
      <div className="panelHead">深访率&nbsp;<FaQuestion className="questionMark" />
        <div className="messageMark"><p>展示在一定时间内深访店铺所占入店量的比例<br /><strong>深访定义</strong>：停留时长超过{}分钟的顾客<br /></p></div></div>
      <div className="panelBody">
        <div className="singleSellerDeepVisitChart" ref={chartEl} />
      </div>
    </div>
    <div className="panel">
      <div className="panelHead">深访率明细</div>
      <div className="panelBody">
        <table className="Table">
          <thead>
            <tr><th>时间</th><th>深访人数</th><th>深访率</th><th>跳出人数</th><th>深访率</th></tr>
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

export default DeepVisit;
