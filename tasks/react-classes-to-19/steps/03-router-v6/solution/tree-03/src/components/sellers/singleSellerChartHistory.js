import React, {useCallback, useRef, useState} from 'react';
import ActiveLink from '../activeLink';
import Calendar from '../calendar';
import Tools from '../tools';
import { Outlet, useParams } from 'react-router-dom';
import echarts from 'echarts/lib/echarts';
import 'echarts/lib/chart/line';
import 'echarts/lib/chart/radar';
import 'echarts/lib/chart/bar';
import 'echarts/lib/chart/pie';
import 'echarts/lib/component/grid';
import 'echarts/lib/component/title';
import 'echarts/lib/component/legend';
import 'echarts/lib/component/tooltip';
import 'echarts/lib/component/dataZoom';



function SingleHistory() {
  const params = useParams();
  const [time, setTime] = useState('day');
  const [selectTime, setSelectTime] = useState('day');
    // componentWillMount(){
      // console.log('componentWillMount  '+new Date().getTime());
    // }
    // componentDidMount(){
      // console.log('componentDidMount  '+new Date().getTime());
    	// this.state.id = this.props.params.id  //获取该商店id

    // }
		// componentDidUpdate(){
      // debugger


		// }
    // componentWillUpdate(nextProps,nextState){
      // 对应的名字写入
      // this.state.name=nextProps.customerFlow.get('name');
      // this.state.id=nextProps.params.id;
    // }
    // componentWillReceiveProps(nextProps,nextState){
      // 对应的名字写入
      // this.setState({
      //   name:nextProps.customerFlow.name,
      //   id:nextProps.params.id
      // })
    // }
		// componentWillUnmount(){

		// }

  // findDOMNode(this.refs.selectTimeN) on a string ref to a host element gave
  // back that same <div>, so a ref on the <div> is the same node.
  const selectTime1 = useRef(null);
  const selectTime2 = useRef(null);

  // Bound in the constructor, so the buttons saw one function for the life of
  // the route; the refs and the setters are stable, so an empty dependency list
  // keeps that.
  const search = useCallback(() => {
    let time1 = selectTime1.current.getElementsByClassName('calendar')[0].getElementsByTagName('input')[0].value;
    let time2 = selectTime2.current.getElementsByClassName('calendar')[0].getElementsByTagName('input')[0].value;
    time1 = Tools.changeTime(time1);
    time2 = Tools.changeTime(time2);

      // 去除红色警示框ClassName
    selectTime1.current.className = selectTime1.current.className.replace(' selectTimeError', '');
    selectTime2.current.className = selectTime1.current.className.replace(' selectTimeError', '');
    const ms1 = new Date(time1).getTime();
    const ms2 = new Date(time2).getTime();
    const now = Date.now();
    let error = false;
    if (ms1 > now) {  // 选择时间超过当前时间，若选择的是今日，也不会超过当前时间
      selectTime1.current.className += ' selectTimeError';// 添加红色警示框ClassName
      error = true;
    }
    if (ms1 >= ms2) { // 时间1大于时间2，错误
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

    setTime(`${time1},${time2}`);
    setSelectTime('');
  }, []);
  const changeTime = useCallback((e) => {
    if (e.target.className === 'active') {
      return;
    }
    switch (e.target.innerText) {
      case '时':
        setTime('hour');
        setSelectTime('hour');
        return;
      case '最近一日':
        setTime('day');
        setSelectTime('day');
        return;
      case '最近一周':
        setTime('week');
        setSelectTime('week');
        return;
      case '最近一月':
        setTime('month');
        setSelectTime('month');
        return;
      case '最近一年':
        setTime('year');
        setSelectTime('year');
        return;
      case '全部':
        setTime('more');
        setSelectTime('more');

    }
  }, []);
        // console.log('render----------time')

  const baseUrl = `/sellers/${params.id}/history/`;
  return (<div className="panelWrapper">
    {/* <p>{this.state.name}</p>*/}
    <div className="selectOption inline">
              快速选择：
                <div className="quickSelect">
                  <ul>
                    {/* <li><a className={selectTime=='hour'?'active':''} onClick={changeTime}>今天</a></li>*/}
                    <li><a className={selectTime == 'day' ? 'active' : ''} onClick={changeTime}>最近一日</a></li>
                    <li><a className={selectTime == 'week' ? 'active' : ''} onClick={changeTime}>最近一周</a></li>
                    <li><a className={selectTime == 'month' ? 'active' : ''} onClick={changeTime}>最近一月</a></li>
                    <li><a className={selectTime == 'year' ? 'active' : ''} onClick={changeTime}>最近一年</a></li>
                    <li><a className={selectTime == 'more' ? 'active' : ''} onClick={changeTime}>全部</a></li>
                  </ul>
                </div>
    </div>
    <div className="selectOption inline">
                自定义时间：
                <div className="selectTime1" ref={selectTime1}><Calendar /></div>
                &nbsp;至&nbsp;
                <div className="selectTime2" ref={selectTime2}><Calendar /></div>
      {/* <div className='sellersSelect2'>
                  <select>
                    <option value ="yesterday">昨日</option>
                    <option value ="lastWeek">近一周</option>
                    <option value="lastMonth">近一月</option>
                    <option value="all">全部</option>
                  </select>
                </div>*/}
      <div className="selectClick">
        <input type="button" value="查询" onClick={search} />
      </div>
    </div>

    <div className="selectOption">
                指标选择：
                <div className="quickSelect">
                  <ul>
                    <li><ActiveLink to={`${baseUrl}customerAvg`} draggable="false">客流量</ActiveLink></li>
                    <li><ActiveLink to={`${baseUrl}customerFlow`} draggable="false">客流量峰值</ActiveLink></li>
                    <li><ActiveLink to={`${baseUrl}customerIn`} draggable="false">入店量</ActiveLink></li>
                    <li><ActiveLink to={`${baseUrl}radar`} draggable="false">总体评价</ActiveLink></li>
                    <li><ActiveLink to={`${baseUrl}stayBar`} draggable="false">驻店时长</ActiveLink></li>
                    <li><ActiveLink to={`${baseUrl}OldOrNew`} draggable="false">新老顾客</ActiveLink></li>
                    <li><ActiveLink to={`${baseUrl}timeSection`} draggable="false">各时间段人数</ActiveLink></li>
                    <li><ActiveLink to={`${baseUrl}deepVisit`} draggable="false">深访率与跳出率</ActiveLink></li>
                    <li><ActiveLink to={`${baseUrl}cycle`} draggable="false">来访周期</ActiveLink></li>
                    <li><ActiveLink to={`${baseUrl}Active`} draggable="false">活跃度</ActiveLink></li>
                  </ul>
                </div>
      {/* 快速选择：
                    <ul>
                      <li><a href='javascript:'>时</a></li>
                      <li><a href='javascript:'>日</a></li>
                      <li><a href='javascript:'>周</a></li>
                      <li><a href='javascript:'>月</a></li>
                      <li><a href='javascript:'>全部</a></li>
                    </ul>*/}
    </div>
    {/*
              <ul ref="adjustingBar" className="adjustingBar">
                   <span>时间参数&nbsp;&nbsp;&nbsp;&nbsp;</span>
                   <li className={time=='hour'?'active':''} onClick={this.adjusting}><a>时</a></li>
                   <li className={time=='day'?'active':''} onClick={this.adjusting}><a>日</a></li>
                   <li className={time=='week'?'active':''} onClick={this.adjusting}><a>周</a></li>
                   <li className={time=='month'?'active':''} onClick={this.adjusting}><a>月</a></li>
                   <div className='chartMessage'>
                   </div>
               </ul>
              */}


    <Outlet context={{time}} />

    {/* <div className="panel halfPanel_1">
          <div className="panelHead">总体评价(雷达图)</div>
          <div className="panelBody">
            <div className="singleSellerRadarChart" ref="singleSellerRadarChart"></div>
          </div>
        </div>
        <div className="panel halfPanel_2">
          <div className="panelHead">驻店时长(柱状图)</div>
          <div className="panelBody">
            <div className="singleSellerStayBarChart" ref="singleSellerStayBarChart"></div>
          </div>
        </div>
        <div className="panel halfPanel_3">
          <div className="panelHead">新老顾客量+率(层叠柱状图？饼图)</div>
          <div className="panelBody">
            <div className="singleSellerOldOrNewChart" ref="singleSellerOldOrNewChart"></div>
          </div>
        </div>
        <div className="panel halfPanel_4">
          <div className="panelHead">各时间段占比(柱状图)</div>
          <div className="panelBody">
            <div className="singleSellerTimeSectionChart" ref="singleSellerTimeSectionChart"></div>
          </div>
        </div>
        <div className="panel halfPanel_3">
          <div className="panelHead">深访率(柱状图)or--</div>
          <div className="panelBody">
            <div className="singleSellerDeepVisitChart" ref="singleSellerDeepVisitChart"></div>
          </div>
        </div>
        <div className="panel halfPanel_4">
          <div className="panelHead">来访周期(竖向 柱状图)+活跃度(饼图)</div>
          <div className="panelBody">
            <div className="singleSellerCycleAndActiveChart" ref="singleSellerCycleAndActiveChart"></div>
          </div>
        </div>*/}

  	</div>);
}

// const mapStateToProps = (state)=>{
//   // debugger;
//   // let fdd=state.getIn(['b','deepVisit'])
//   // let d=state.getIn(['b','deepVisit']).toJS()
//   return {
//     customerFlow:state.getIn(['b','customerFlow']),
//     radar:state.getIn(['b','radar']),
//     stayBar:state.getIn(['b','stayBar']),
//     OldOrNew:state.getIn(['b','OldOrNew']),
//     timeSection:state.getIn(['b','timeSection']),
//     deepVisit:state.getIn(['b','deepVisit']),
//     cycleAndActive:state.getIn(['b','cycleAndActive'])
//     }
// }
/*


    cycleAndActive:state.getIn(['b','cycleAndActive']).toJS()*/

// let singleHistory=connect(mapStateToProps,sellersAction)(_singleHistory);

export default SingleHistory;
