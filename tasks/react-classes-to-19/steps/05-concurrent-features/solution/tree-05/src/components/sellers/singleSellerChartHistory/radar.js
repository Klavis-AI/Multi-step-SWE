import React, {useCallback, useEffect, useRef, useState} from 'react';
import {useSelector} from 'react-redux';
import { useParams } from 'react-router-dom';
import echarts from 'echarts/lib/echarts';
import 'echarts/lib/chart/line';
import Tools from '../../tools';
import {useSingleSellerRadarQuery} from '../../../store/api';
import { FaQuestion } from 'react-icons/fa';

function Radar() {
  const params = useParams();
  const radar = useSelector(state => state.sellers.radar);

  // this.state held three different kinds of thing.  The echarts instance and
  // the debounce handle were assigned to and never read while rendering, so
  // they are refs; the two lists drive the table, so they are state; and the
  // chart container, which used to be a string ref read back through
  // findDOMNode, is now a ref on the element itself.  this.state.time was
  // assigned the prop in componentDidMount and never read again.
  const chartEl = useRef(null);
  const chart = useRef(null);
  const resizeHandler = useRef(null);
  const [nameList, setNameList] = useState('');
  const [numList, setNumList] = useState('');

  const resizeFun = useCallback(() => {
    if (resizeHandler.current) {
      clearTimeout(resizeHandler.current);
    }
    resizeHandler.current = setTimeout(() => {
      chart.current.resize();
    }, 100);
  }, []);

  useSingleSellerRadarQuery(params.id);

  useEffect(() => {
		// debugger
    chart.current = echarts.init(chartEl.current);
    chart.current.showLoading();
    window.addEventListener('resize', resizeFun);
    return () => {
      // 切换路由销毁echarts实例
      chart.current.dispose();
      window.removeEventListener('resize', resizeFun);
    };
  }, []);

  // componentWillReceiveProps, which never ran on mount.  It must not run on
  // mount here either: on a revisit the chart is a fresh instance showing its
  // loading mask, and the class left it there until the request it had just
  // re-sent came back and moved the store.
  const mounted = useRef(false);
  useEffect(() => {
    if (!mounted.current) { mounted.current = true; return; }
		// debugger
    const next = Tools.copyOption(radar);
    if (next.series.data && next.series.data[0] && next.series.data[0][0]) {
      const names = [];
      next.radar.indicator.forEach((item) => {
        names.push(item.name);
      });
      setNumList(next.series.data[0]);
      setNameList(names);
    }

		// if(this.state.time!=nextProps.time){
			// 不支持自定义时间，返回结果是开店以来的结果
			// this.setState({time:nextProps.time});
			// this.props.singleSellerRadar(nextProps.params.id);
		// }
    chart.current.setOption(next);
    chart.current.hideLoading();
  }, [radar]);

  const rows = [];
  if (numList) {
    numList.forEach((item, i) => {
      rows.push(<tr key={i}><td>开店以来</td><td>{nameList[i]}</td><td>{numList[i]}</td></tr>);
    });
  }
  return (<div>
    <div className="panel">
      <div className="panelHead">总体评价&nbsp;<FaQuestion className="questionMark" />
        <div className="messageMark"><p>展示商家开店以来的总体评价<br /><strong>评价标准</strong>：100分为满分，根据各项指标在全部商家的排名，再乘以相关系数得出<br /></p></div></div>
      <div className="panelBody">
        <div className="singleSellerRadarChart" ref={chartEl} />
      </div>
    </div>
    <div className="panel">
      <div className="panelHead">总体评价明细</div>
      <div className="panelBody">
        <table className="Table">
          <thead>
            <tr><th>时间范围</th><th>指标</th><th>评价(百分制)</th></tr>
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
// left to validate is what the route still hands in.

export default Radar;
