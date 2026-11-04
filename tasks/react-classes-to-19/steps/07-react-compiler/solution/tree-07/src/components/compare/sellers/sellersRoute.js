import React, {Suspense, useEffect, useLayoutEffect, useRef, useState, useTransition} from 'react';
import {useSelector} from 'react-redux';
import { Outlet, useOutletContext } from 'react-router-dom';
import Calendar from '../../calendar';
import Tools from '../../tools';
import 'echarts/lib/chart/line';
import 'echarts/lib/chart/bar';
import 'echarts/lib/component/title';
import 'echarts/lib/component/legend';
import 'echarts/lib/component/tooltip';
import {useCompareSellersListQuery} from '../../../store/api';

/* Read nothing off the instance and was called for its return value.  The one
   thing that made it look like a method was the assignment to state.time2 on
   the way out, and nothing ever read time2 - the legend that would have used it
   is commented out in every child - so the write goes with it. */
function getTodayTime() {
  const year = new Date().getFullYear();
  let month = new Date().getMonth() + 1;
  month = month >= 10 ? month : `0${month}`;
  let day = new Date().getDate();
  day = day >= 10 ? day : `0${day}`;
  return `${year}-${month}-${day}`;
}

function SellersRoute() {
  const {changeActiveRoute} = useOutletContext();
  const sellersList = useSelector(state => state.compare.sellersList);

  /* The 商家 list was fetched once from componentDidMount and never again, so a
     query with no argument covers it.  The layout effect below has no
     dependency list, which means it runs after every commit; selecting nothing
     out of the query keeps its loading transitions from adding commits the
     class never had, and the list still arrives through the slice above. */
  useCompareSellersListQuery(undefined, {selectFromResult: () => ({})});

        // this.changeSellerName=this.changeSellerName.bind(this);
            // compareCustomerOldOrNewChart:'',
            // resizeHandler:null,
            // Data:[],
  const [sellersAndTime, setSellersAndTime] = useState('');   // 传给子组件的商家和时间参数

  /* 查询 is the one control on this screen that changes what the panels below
     draw: `sellersAndTime` goes down the <Outlet>'s context and both of them
     refetch on it.  So that write is a transition - but only the one in the
     search handler below.  The layout effect above writes the same state to put
     the first two 商家 into the frame the lifecycle wrote them in, and a
     transition there would take it back out of that frame. */
  const [, startCompareChange] = useTransition();
  const [selectTime, setSelectTime] = useState('day');
  const [time, setTime] = useState('');          // 要请求的time参数，有多个

  // findDOMNode(this.refs.x) on a string ref to a host element gave back that
  // element; a ref on the same <div> is the same node.  time1, time2, seller1
  // and seller2 were state fields no one ever assigned - every use of those
  // names is a local const - so they have nothing to become.
  const selectSeller1 = useRef(null);
  const selectSeller2 = useRef(null);
  const selectTime1 = useRef(null);
  const selectTime2 = useRef(null);

    // changeSellerName(name){
        // setSellerName(name);
    // }
  useEffect(() => {
      // console.log('componentWillMount')
         // console.log('componentDidMount');
     // console.log('this.props::::')
    changeActiveRoute();
  }, []);

  /* componentWillUpdate, which ran before every update and never before the
     first render.  It did two things the render behind it depended on: it wrote
     the first two 商家 of the list into the two uncontrolled inputs, and it put
     the parameter string straight onto the state object, so the render it
     preceded already handed that string to the children.  A layout effect with
     no dependency list runs after every commit and before the browser paints,
     which keeps both halves in the frame the lifecycle had them in; the skip on
     mount is the lifecycle's own.  Note the condition is "no 商家 chosen yet",
     not "the list changed": arriving here with a list already in the store and
     nothing chosen is what defaults the pair. */
  const mounted = useRef(false);
  useLayoutEffect(() => {
    if (!mounted.current) { mounted.current = true; return; }
    if (sellersList) {
      const seller1 = selectSeller1.current.getElementsByTagName('input')[0];
      const seller2 = selectSeller2.current.getElementsByTagName('input')[0];
      const time1 = getTodayTime();
            // 没有选择商家才默认选择第1、2个
      if (!sellersAndTime) {
        seller1.value = sellersList[0];
        seller2.value = sellersList[1];
        setSellersAndTime(`${seller1.value},${seller2.value}/${time1},${time1}`);
      }
    }
  });
    // componentWillUnmount(){
        // console.log('componentWillUnmount');
    // }
    // componentWillReceiveProps(){
    // }

  /* Nothing calls this: the 对比时间范围 buttons that did are commented out in
     the markup below, and so is every read of selectTime.  Kept because the
     class kept it. */
  const changeTime = (e) => {
    if (e.target.className === 'active') {
      return;
    }
    switch (e.target.innerText) {
        /* case '时':
          setTime('hour');
          setSelectTime('hour');
          return;*/
      case '日':
        setTime('day');
        setSelectTime('day');
        return;
      case '周':
        setTime('week');
        setSelectTime('week');
        return;
      case '月':
        setTime('month');
        setSelectTime('month');

    }
  };
  const search = () => {
    let time1 = selectTime1.current.getElementsByClassName('calendar')[0].getElementsByTagName('input')[0].value;
    let time2 = selectTime2.current.getElementsByClassName('calendar')[0].getElementsByTagName('input')[0].value;
    time1 = Tools.changeTime(time1);
    time2 = Tools.changeTime(time2);
        // 去除红色警示框ClassName
    selectTime1.current.className = selectTime1.current.className.replace(' selectTimeError', '');
    selectTime2.current.className = selectTime1.current.className.replace(' selectTimeError', '');
    selectSeller1.current.getElementsByTagName('input')[0].className = selectSeller1.current.getElementsByTagName('input')[0].className.replace(' selectTimeError', '');
    selectSeller2.current.getElementsByTagName('input')[0].className = selectSeller2.current.getElementsByTagName('input')[0].className.replace(' selectTimeError', '');
        // 确定商家名有没有为空
    const seller1 = selectSeller1.current.getElementsByTagName('input')[0];
    const seller2 = selectSeller2.current.getElementsByTagName('input')[0];
    if (!seller1.value.trim().length) {
      seller1.className += ' selectTimeError';
      return;
    }
    if (!seller2.value.trim().length) {
      seller2.className += ' selectTimeError';
      return;
    }
    if (seller2.value.trim() === seller1.value.trim()) { // 对比的商家是同一家
      seller2.className += ' selectTimeError';
      return;
    }
        // 时间有没有错
    const ms1 = new Date(time1).getTime();
    const ms2 = new Date(time2).getTime();
    const now = Date.now();
    let error = false;
    if (ms1 > now) {  // 选择时间超过当前时间，若选择的是今日，也不会超过当前时间
      selectTime1.current.className += ' selectTimeError';// 添加红色警示框ClassName
      error = true;
    }
    if (ms1 > ms2) { // 时间1大于时间2，错误
      selectTime2.current.className += ' selectTimeError';
      error = true;
    }
    if (ms2 > now) {  // 选择时间超过当前时间，若选择的是今日，也不会超过当前时间
      selectTime2.current.className += ' selectTimeError';// 添加红色警示框ClassName
      error = true;
    }
    if (error) {
      return false;
    }
        // 向子组件发送参数
    startCompareChange(() => {
      setSellersAndTime(`${seller1.value},${seller2.value}/${time1},${time2}`/* +'|'+selectTime*/);
    });
        // console.log('参数：'+seller1.value+'   '+seller2.value+'   '+time1+'   '+time2)
        // dispatch(compareAction.customerOldOrNewInit(time1+'|'+time2+'|'+selectTime));
        // time1.current=time1;
        // time2.current=time2;
  };

        // sellersList插入dataList
  const dataList = [];
  if (sellersList) {
    sellersList.forEach((item, index) => {
      dataList.push(<option key={index}>{item}</option>);
    });
  }
  return (<div className="chartWrapper">

    <div className="selectOption inline">
                  商家一：
                  <div className="selectSeller1 selectTime1" ref={selectSeller1}>
                    <input list="selectSeller1" className="selectSeller1" />
                    <datalist id="selectSeller1">
                      {dataList}
                    </datalist>
                  </div>
                  &nbsp;&nbsp;&nbsp;商家二：&nbsp;
                  <div className="selectSeller2 selectTime2" ref={selectSeller2}>
                    <input list="selectSeller2" className="selectSeller2" />
                    <datalist id="selectSeller2">
                      {dataList}
                    </datalist>
                  </div>
    </div>

    <div className="selectOption inline">
                  时间：&nbsp;&nbsp;&nbsp;
                  <div className="selectTime1" ref={selectTime1}><Calendar /></div>
                  &nbsp;&nbsp;至&nbsp;&nbsp;
                  <div className="selectTime1" ref={selectTime2}><Calendar /></div>
      {/* &nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;对比时间范围：
                  <div className='quickSelect defaultCursor'>
                    <ul>
                          <li><a className={selectTime=='hour'?'active':''} onClick={changeTime}>时</a></li>
                          <li><a className={selectTime=='day'?'active':''} onClick={changeTime}>日</a></li>
                          <li><a className={selectTime=='week'?'active':''} onClick={changeTime}>周</a></li>
                          <li><a className={selectTime=='month'?'active':''} onClick={changeTime}>月</a></li>
                      </ul>
                  </div>*/}
      <div className="selectClick">
        <input type="button" value="查询" onClick={search} />
      </div>
    </div>

    {/* {children && React.cloneElement(children, {changeSellerName})}*/}
    {/* The seller pickers and the 查询 button above are the shell here, and
        they are also the controls a visitor uses while the body is loading.
        Putting the boundary around the <Outlet> alone is what keeps them
        clickable through it. */}
    <Suspense fallback={<div className="panelLoading">加载中…</div>}>
      <Outlet context={{sellersAndTime}} />
    </Suspense>
  </div>);
}
/* && React.cloneElement(children, {changeSellerName})*/

export default SellersRoute;
