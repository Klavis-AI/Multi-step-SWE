import React, {useCallback, useEffect, useRef} from 'react';
import {useSelector} from 'react-redux';
import { useOutletContext, useParams } from 'react-router-dom';
import echarts from 'echarts/lib/echarts';
import 'echarts/lib/chart/line';
import Tools from '../../tools';
import {useSingleSellerCycleAndActiveQuery} from '../../../store/api';

function StayBar() {
  const params = useParams();
  const {time} = useOutletContext();
  const cycleAndActive = useSelector(state => state.sellers.cycleAndActive);

  const chartEl = useRef(null);
  const chart = useRef(null);
  const resizeHandler = useRef(null);

  const resizeFun = useCallback(() => {
    if (resizeHandler.current) {
      clearTimeout(resizeHandler.current);
    }
    resizeHandler.current = setTimeout(() => {
      chart.current.resize();
    }, 100);
  }, []);

  // componentDidMount fetched with the time it had just copied out of props,
  // and componentWillUpdate fetched again whenever that prop changed - the two
  // together are one effect keyed on the time.  params.id is read but is not a
  // dependency, because the class refetched on a change of time alone.
  useSingleSellerCycleAndActiveQuery({id: params.id, param: time});

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

  // This one is componentDidUpdate, not componentWillReceiveProps: the class
  // pushed the option after every update, with no guard on the data and no
  // comparison against the previous props.  An effect with no dependency list
  // runs on exactly those commits, and the mount skip is componentDidUpdate's
  // own - it does not run alongside componentDidMount.  Nothing in here sets
  // state, so re-running on every commit cannot feed itself.
  const mounted = useRef(false);
  useEffect(() => {
    if (!mounted.current) { mounted.current = true; return; }
		// debugger
    chart.current.setOption(Tools.copyOption(cycleAndActive));
    chart.current.hideLoading();
  });

  return (<div className="panel">
    <div className="panelHead">来访周期及活跃度</div>
    <div className="panelBody">
      <div className="singleSellerCycleAndActiveChart" ref={chartEl} />
    </div>
  </div>);
}

// The store slice and the bound action creator the class validated are no
// longer props - they are read with useSelector and useDispatch - so what is
// left to validate is what the route and the parent still hand in.

export default StayBar;
