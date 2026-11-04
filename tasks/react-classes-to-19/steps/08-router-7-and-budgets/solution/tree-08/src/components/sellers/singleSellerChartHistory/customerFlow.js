import React, {useEffect, useRef} from 'react';
import {useSelector} from 'react-redux';
import { useOutletContext, useParams } from 'react-router';
import echarts from 'echarts/lib/echarts';
import 'echarts/lib/chart/line';
import 'echarts/lib/component/toolbox';
import Tools from '../../tools';
import {useSingleSellerCustomerFlowInitQuery} from '../../../store/api';
import { FaQuestion } from 'react-icons/fa';

const tableSpace = 1;

function CustomerFlow() {
  const params = useParams();
  const {time} = useOutletContext();
  const customerFlow = useSelector(state => state.sellers.customerFlow);

  const chartEl = useRef(null);
  const chart = useRef(null);
  const resizeHandler = useRef(null);

  /* The table's values were 3 useState that the effect below assigned every
     time the slice changed, which is a render's worth of work done a render
     late: each one is a read of the response the store already holds.  Render
     works them out where they are used, and the state they replace goes with
     them - a useState whose only writer was that effect was never state. */
  const hasData = customerFlow.series && customerFlow.series[0] && customerFlow.series[0].data && customerFlow.series[0].data[0];
  const timeList = hasData ? customerFlow.xAxis.data : '';
  const numList = hasData ? customerFlow.series[0].data : '';
  const percentList = hasData ? customerFlow.series[1].data : '';

  // componentDidMount fetched with the time it had just copied out of props,
  // and componentWillReceiveProps fetched again whenever that prop changed.
  // params.id is read but is not a dependency, because the class refetched on a
  // change of time alone.
  useSingleSellerCustomerFlowInitQuery({id: params.id, param: time});

  useEffect(() => {
    chart.current = echarts.init(chartEl.current);
    chart.current.showLoading();
    const resizeFun = () => {
      if (resizeHandler.current) {
        clearTimeout(resizeHandler.current);
      }
      resizeHandler.current = setTimeout(() => {
        chart.current.resize();
      }, 100);
    };

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
    const next = Tools.copyOption(customerFlow);
    if (next.series && next.series[0] && next.series[0].data && next.series[0].data[0]) {
      chart.current.setOption(next);
      chart.current.hideLoading();
    }
  }, [customerFlow]);

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
      <div className="panelHead">客流量峰值&nbsp;<FaQuestion className="questionMark" />
        <div className="messageMark"><p>展示商家在一定时间内的客流峰值及总体占比<br /><strong>客流峰值</strong>：时间段内客流最高的值<br /><strong>客流总体占比</strong>：时间段内客流占整个商城的比例</p></div></div>
      <div className="panelBody">
        <div className="singleSellerCustomerFlowChart" ref={chartEl} />
      </div>
    </div>
    <div className="panel">
      <div className="panelHead">客流量峰值明细</div>
      <div className="panelBody">
        <table className="Table">
          <thead>
            <tr><th>时间</th><th>客流量峰值</th><th>客流总体占比</th></tr>
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

export default CustomerFlow;
