import React, {useCallback, useEffect, useRef, useState} from 'react';
import {useSelector} from 'react-redux';
import { useOutletContext, useParams } from 'react-router-dom';
import echarts from 'echarts/lib/echarts';
import 'echarts/lib/chart/line';
import 'echarts/lib/component/toolbox';
import Tools from '../../tools';
import {useSingleSellerCustomerInInitQuery} from '../../../store/api';
import { FaQuestion } from 'react-icons/fa';

const tableSpace = 1;

function CustomerIn() {
  const params = useParams();
  const {time} = useOutletContext();
  const customerIn = useSelector(state => state.sellers.customerIn);

  const chartEl = useRef(null);
  const chart = useRef(null);
  const resizeHandler = useRef(null);
  const [timeList, setTimeList] = useState('');
  const [numList, setNumList] = useState('');
  const [percentList, setPercentList] = useState('');

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
  useSingleSellerCustomerInInitQuery({id: params.id, param: time});

  useEffect(() => {
    chart.current = echarts.init(chartEl.current);
    chart.current.showLoading();
    window.addEventListener('resize', resizeFun);
    return () => {
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
    const next = Tools.copyOption(customerIn);
    if (next.series && next.series[0] && next.series[0].data && next.series[0].data[0]) {
      setTimeList(next.xAxis.data);
      setNumList(next.series[0].data);
      setPercentList(next.series[1].data);
      chart.current.setOption(next);
      chart.current.hideLoading();
    }
  }, [customerIn]);

  const rows = [];
  if (timeList) {
    timeList.forEach((item, i) => {
      if (!(i % tableSpace)) {
        rows.push(<tr key={i}><td>{timeList[i]}</td><td>{numList[i]}</td><td>{percentList[i]}%</td></tr>);
      }
    });
  }
  return (<div>
    <div className="panel">
      <div className="panelHead">商店入店量&nbsp;<FaQuestion className="questionMark" />
        <div className="messageMark"><p>展示商家在一定时间内的入店量及入店率<br /><strong>入店量</strong>：时间段内进入商店的人数<br /><strong>入店率</strong>：时间段内，从进入店铺内的客流量与店铺门口经过的客流量的比率。</p></div></div>
      <div className="panelBody">
        <div className="singleSellerCustomerInChart" ref={chartEl} />
      </div>
    </div>
    <div className="panel">
      <div className="panelHead">商店入店量明细</div>
      <div className="panelBody">
        <table className="Table">
          <thead>
            <tr><th>时间</th><th>入店量</th><th>入店率</th></tr>
          </thead>
          <tbody>
            {rows}
          </tbody>
        </table>
        <div />
      </div>
    </div>
  </div>);
}

// The store slice and the bound action creator the class validated are no
// longer props - they are read with useSelector and useDispatch - so what is
// left to validate is what the route and the parent still hand in.

export default CustomerIn;
