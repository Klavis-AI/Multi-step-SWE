import React, {useEffect, useRef, useState} from 'react';
import {useSelector} from 'react-redux';
import { useOutletContext, useParams } from 'react-router-dom';
import echarts from 'echarts/lib/echarts';
import 'echarts/lib/chart/line';
import 'echarts/lib/component/toolbox';
import Tools from '../../tools';
import {useSingleSellerCustomerAvgInitQuery} from '../../../store/api';
import { FaQuestion } from 'react-icons/fa';

const tableSpace = 1;

function CustomerAvg() {
  const params = useParams();
  const {time} = useOutletContext();
  const customerAvg = useSelector(state => state.sellers.customerAvg);

  const chartEl = useRef(null);
  const chart = useRef(null);
  const resizeHandler = useRef(null);
  const [timeList, setTimeList] = useState('');
  const [numList, setNumList] = useState('');
  const [percentList, setPercentList] = useState('');

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
  useSingleSellerCustomerAvgInitQuery({id: params.id, param: time});

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

	// changeTime = ()=>{
	// 	this.props.singleSellerCustomerAvgInit(this.props.params.id,this.state.time);
	// }

  // The rest of componentWillReceiveProps, which never ran on mount.  It must
  // not run on mount here either: on a revisit the chart is a fresh instance
  // showing its loading mask, and the class left it there until the request it
  // had just re-sent came back and moved the store.
  const mounted = useRef(false);
  useEffect(() => {
    if (!mounted.current) { mounted.current = true; return; }
    const next = Tools.copyOption(customerAvg);
    if (next.series && next.series[0] && next.series[0].data && next.series[0].data[0]) {
      setTimeList(next.xAxis.data);
      setNumList(next.series[0].data);
      setPercentList(next.series[1].data);
      chart.current.setOption(next);
      chart.current.hideLoading();
    }
  }, [customerAvg]);

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
      <div className="panelHead">客流量&nbsp;<FaQuestion className="questionMark" />
        <div className="messageMark"><p>展示商家在一定时间内的客流平均值及总体占比<br /><strong>客流量</strong>：时间段内客流的平均值<br /><strong>客流总体占比</strong>：时间段内客流占整个商城的比例</p></div></div>
      <div className="panelBody">
        <div className="singleSellerCustomerAvgChart" ref={chartEl} />
      </div>
    </div>
    <div className="panel">
      <div className="panelHead">客流量明细</div>
      <div className="panelBody">
        <table className="Table">
          <thead>
            <tr><th>时间</th><th>客流量</th><th>客流总体占比</th></tr>
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

export default CustomerAvg;
