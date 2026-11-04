import React, {useCallback, useEffect, useRef, useState} from 'react';
import {useSelector} from 'react-redux';
import { useOutletContext, useParams } from 'react-router-dom';
import echarts from 'echarts/lib/echarts';
import 'echarts/lib/chart/line';
import 'echarts/lib/component/grid';
import 'echarts/lib/component/title';
import 'echarts/lib/component/legend';
import 'echarts/lib/component/tooltip';
import 'echarts/lib/component/dataZoom';
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
     payload has already landed, and the second is what the 10s interval calls;
     both read the seller id out of a ref.  So both are triggered where the
     thunk used to go out, and both triggers are stable across renders. */
  const [loadInit] = useLazySingleSellerCustomerNumInitQuery();
  const [loadFetch] = useLazySingleSellerCustomerNumFetchQuery();

  const chartEl = useRef(null);
  const chart = useRef(null);  // 商家图表
  const timer = useRef(null);    // 定时器
  const resizeHandler = useRef(null);
  const [now, setNow] = useState({});
  const [yesterday, setYesterday] = useState({});
  const [timeList, setTimeList] = useState('');
  const [numList, setNumList] = useState('');
  const [doorNumList, setDoorNumList] = useState();
  const [percentList, setPercentList] = useState('');

  // this.fetchData was bound once in the class body and read this.props.params.id
  // at call time, so the interval started on mount always polled whichever seller
  // was on screen.  A ref that trails the prop says the same thing, and keeps
  // fetchData itself stable so the interval never has to be rebuilt for it.
  const currentId = useRef(params.id);
  useEffect(() => {
    currentId.current = params.id;
  }, [params.id]);
  const fetchData = useCallback(() => {
		// debugger
    loadFetch(currentId.current);
  }, [loadFetch]);

  const resizeFun = useCallback(() => {
    if (resizeHandler.current) {
      clearTimeout(resizeHandler.current);
    }
    if (chart.current) {
      resizeHandler.current = setTimeout(() => {
        chart.current.resize();
        chart.current.resize();
      }, 100);
    }
  }, []);

  // componentWillMount ran the scroll, the first fetch and the name hand-off,
  // then componentDidMount set the chart and the poll up; one mount effect keeps
  // them in that order.  componentWillUnmount is its cleanup.
  useEffect(() => {
        // debugger;
    scrollTo(0, 0);
    const id = params.id;  // 根据路由获取该商店id
    loadInit(id);
    changeSellerName(id);

    	chart.current = echarts.init(chartEl.current);
    chart.current.showLoading();

    	// 定时刷新
    timer.current = setInterval(fetchData, timerTime);

    window.addEventListener('resize', resizeFun);
    return () => {
			// console.log('componentWillUnmount...')
      clearInterval(timer.current);
      chart.current.dispose();
      window.removeEventListener('resize', resizeFun);
    };
  }, []);

  /* componentWillReceiveProps' first branch: 用上次存的路由和这次比较，不为空且
     不同的话表示在不同商家间跳转。this.state.param was written at the end of every
     componentWillReceiveProps that found data, so once the first response had
     landed it always held the id of the render that had just happened - which
     makes the old test "a payload has arrived AND params.id just changed", and
     an effect keyed on params.id behind that same one-shot flag is that test. */
  const hasData = useRef(false);
  useEffect(() => {
    if (hasData.current) {
      clearInterval(timer.current);
      loadInit(params.id);
      timer.current = setInterval(fetchData, timerTime);
      changeSellerName(params.id);
    }
  }, [params.id]);

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
      const nextNow = {numLen: '', num: '', doorNumLen: '', doorNum: '', percentLen: '', percent: ''};
      const nextYesterday = next.xAxis[0].yesterday;
            // 四个分别是时间、客流量、门前客流、客流占比
      const nextTimeList = next.xAxis[0].data.reverse();
      const nextNumList = next.series[0].data.reverse();
      const nextDoorNumList = next.series[1].data.reverse();
      const nextPercentList = next.series[2].data.reverse();

      nextNow.numLen = nextNumList.length;
      nextNow.num = nextNumList[nextNow.numLen - 1];
      nextNow.doorNumLen = nextDoorNumList.length;
      nextNow.doorNum = nextDoorNumList[nextNow.doorNumLen - 1];
      nextNow.percentLen = nextPercentList.length;
      nextNow.percent = nextPercentList[nextNow.percentLen - 1];
      setNow(nextNow);
      setYesterday(nextYesterday);
      setTimeList(nextTimeList);
      setNumList(nextNumList);
      setDoorNumList(nextDoorNumList);
      setPercentList(nextPercentList);

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
        <p>当前客流：{now.num} {/* {+''=='true'?<span className='up'>&nbsp;↑</span> :<span className='down'>&nbsp;↓</span>} */}</p>
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
