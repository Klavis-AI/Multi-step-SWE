import React, {useEffect, useRef} from 'react';
import {useSelector} from 'react-redux';
import { useOutletContext, useParams } from 'react-router';
import echarts from 'echarts/lib/echarts';
import 'echarts/lib/chart/line';
import 'echarts/lib/component/grid';
import 'echarts/lib/component/title';
import 'echarts/lib/component/tooltip';
import 'echarts/lib/component/dataZoom';
import 'echarts/lib/component/toolbox';
// legend goes last: echarts draws components in the order they register,
// and the entry chunk used to register this one after toolbox and dataZoom.
import 'echarts/lib/component/legend';
import { FaQuestion } from 'react-icons/fa';
import Tools from '../tools';
import {useLazySingleSellerCustomerNumInitQuery,
  useLazySingleSellerCustomerNumFetchQuery} from '../../store/api';

const tableSpace = 3;
const timerTime = 1000 * 10;       // 时间间隔

function Chart() {
  const params = useParams();
  const {changeSellerName} = useOutletContext();
  const customerNum = useSelector(state => state.sellers.customerNum);

  /* Neither fetch follows a value this component renders from.  The first is
     asked for on mount and again from the branch below that only fires once a
     payload has already landed, and the second is what the 10s interval calls
     with the seller id it reads out of a ref.  So both are triggered where the
     thunk used to go out, and both triggers are stable across renders. */
  const [loadInit] = useLazySingleSellerCustomerNumInitQuery();
  const [loadFetch] = useLazySingleSellerCustomerNumFetchQuery();

  const chartEl = useRef(null);
  const chart = useRef(null);  // 商家图表
  const timer = useRef(null);    // 定时器
  const resizeHandler = useRef(null);

  /* 四个分别是时间、客流量、门前客流、客流占比, and the three 当前 readings above
     them are the last entry of three of those.  All of it was useState that the
     effect below assigned on every round that carried data, which is a render's
     worth of work done a render late: each one is a read of the response the
     store already holds.  Render works them out where they are used, and the
     state they replace goes with them - a useState whose only writer was that
     effect was never state.  reverse() rewrites the array it is called on and
     these are the reducer's, so each list is reversed on a copy of its own. */
  const hasPayload = customerNum.series[0] && customerNum.series[0].data && customerNum.series[0].data[0];
  const timeList = hasPayload ? customerNum.xAxis[0].data.slice().reverse() : '';
  const numList = hasPayload ? customerNum.series[0].data.slice().reverse() : '';
  const doorNumList = hasPayload ? customerNum.series[1].data.slice().reverse() : '';
  const percentList = hasPayload ? customerNum.series[2].data.slice().reverse() : '';
  const yesterday = hasPayload ? customerNum.xAxis[0].yesterday : {};
  // 当前客流.  The object the class kept this in had six fields and render read
  // one of them.
  const num = hasPayload ? numList[numList.length - 1] : '';

  // this.fetchData was bound once in the class body and read this.props.params.id
  // at call time, so the interval started on mount always polled whichever seller
  // was on screen.  A ref that trails the prop says the same thing, and the
  // interval below reads it rather than being rebuilt for a new id.
  const currentId = useRef(params.id);
  useEffect(() => {
    currentId.current = params.id;
  }, [params.id]);

  const resizeFun = () => {
    if (resizeHandler.current) {
      clearTimeout(resizeHandler.current);
    }
    if (chart.current) {
      resizeHandler.current = setTimeout(() => {
        chart.current.resize();
        chart.current.resize();
      }, 100);
    }
  };

  /* componentWillMount ran the scroll, the first fetch and the name hand-off,
     and componentWillReceiveProps' first branch ran the fetch and the hand-off
     again whenever the route moved to another 商家: 用上次存的路由和这次比较，不
     为空且不同的话表示在不同商家间跳转。this.state.param was written at the end of
     every componentWillReceiveProps that found data, so once the first response
     had landed it always held the id of the render that had just happened -
     which makes the old test "a payload has arrived AND params.id just
     changed".  An effect keyed on the id is both: its first run is the mount,
     and every later run is that branch, behind the same one-shot flag.  The two
     triggers and the hand-off are stable, so the id is the only thing that
     moves this. */
  const hasData = useRef(false);
  const idMounted = useRef(false);
  useEffect(() => {
        // debugger;
    if (!idMounted.current) {
      idMounted.current = true;
      scrollTo(0, 0);
      const id = params.id;  // 根据路由获取该商店id
      loadInit(id);
      changeSellerName(id);
        // 定时刷新
      timer.current = setInterval(() => {
        loadFetch(currentId.current);
      }, timerTime);
      return;
    }
    if (hasData.current) {
      clearInterval(timer.current);
      loadInit(params.id);
      timer.current = setInterval(() => {
        loadFetch(currentId.current);
      }, timerTime);
      changeSellerName(params.id);
    }
  }, [params.id, loadInit, loadFetch, changeSellerName]);

  // componentDidMount set the chart up and componentWillUnmount is its cleanup;
  // the poll the effect above owns is cleared there too, where the class
  // cleared it.
  useEffect(() => {
    chart.current = echarts.init(chartEl.current);
    chart.current.showLoading();

    window.addEventListener('resize', resizeFun);
    return () => {
			// console.log('componentWillUnmount...')
      clearInterval(timer.current);
      chart.current.dispose();
      window.removeEventListener('resize', resizeFun);
    };
  }, []);

  // The rest of componentWillReceiveProps, which never ran on mount and must not
  // run on mount here either.  Every poll puts a fresh value in the store, so
  // this fires once per response just as the class did.
  const mounted = useRef(false);
  useEffect(() => {
    if (!mounted.current) { mounted.current = true; return; }
        // 根据服务器传回数据显示商家名，现在是以根据路由名字显示
        // changeSellerName(customerNum.get('name'));
    const next = Tools.copyOption(customerNum);
    if (next.series[0] && next.series[0].data && next.series[0].data[0]) {
            // 四个分别是时间、客流量、门前客流、客流占比.  The chart is drawn from the
            // reversed arrays, so the reverse stays ahead of setOption.
      next.xAxis[0].data.reverse();
      next.series[0].data.reverse();
      next.series[1].data.reverse();
      next.series[2].data.reverse();

      hasData.current = true;
      chart.current.setOption(next);
      chart.current.hideLoading();
    }
  }, [customerNum]);

  const rows = [];
  if (timeList) {
    timeList.forEach((item, i) => {
      if (!(i % tableSpace)) {
        rows.push(<tr key={i}><td>{timeList[i]}</td><td>{doorNumList[i]}</td><td>{numList[i]}</td><td>{percentList[i]}%</td></tr>);
      }
    });
  }


  	return (<div className="panelWrapper">
    {/* <p>{this.state.name}</p>*/}
    <div className="topMessage">
      <div className="message message1"><div>
        <p>当前客流：{num} {/* {+''=='true'?<span className='up'>&nbsp;↑</span> :<span className='down'>&nbsp;↓</span>} */}</p>
        <p>当前时间：{timeList ? timeList[timeList.length - 1] : ''}</p>
      </div></div>
      <div className="message message2"><div>
        <p>当前门前客流：{doorNumList ? doorNumList[doorNumList.length - 1] : ''}</p>
        <p>客流占比：{percentList ? percentList[percentList.length - 1] : ''}%</p>
      </div></div>
      <div className="message message3"><div>
        <p>昨日高峰客流：{yesterday.num}</p>
        <p>昨日高峰出现时间：{yesterday.time}</p>
      </div></div>
    </div>
    <div className="panel">
      <div className="panelHead">实时客流量&nbsp;<FaQuestion className="questionMark" />
        <div className="messageMark"><p>展示商家实时客流量、门前客流量及客流总体占比<br /><strong>客流量</strong>：当前在店铺内的人数<br /><strong>门前客流量</strong>：经过店铺门口的人数<strong>客流总体占比</strong>：店铺内的人数占整个商城的人数比例</p></div></div>
      <div className="panelBody">
        <div className="singleSellerCustomerNumChart" ref={chartEl} />
      </div>
    </div>
    <div className="panel">
      <div className="panelHead">当前实时客流数据</div>
      <div className="panelBody">
        <table className="Table">
          <thead>
            <tr><th>时间</th><th>门前客流量</th><th>店铺客流量</th><th>客流总体占比</th></tr>
          </thead>
          <tbody>
            {rows}
          </tbody>
        </table>
      </div>
    </div>
    {/*
          <div className="panel">
              <div className="panelHead">商家简介</div>
              <div className="panelBody">
                  <div className="singleSellerIntroduction" ref="singleSellerIntroduction"></div>
              </div>
          </div>
          */}
  	</div>);
}


export default Chart;
