/**
 * Created by HH on 2017/3/16.
 */
import React, {useCallback, useEffect, useRef, useState} from 'react';
import {useDispatch, useSelector} from 'react-redux';
import Immutable from 'immutable';
import { Router, Route, IndexRoute, hashHistory, Link, Redirect} from 'react-router';
import sellersAction from '../../actions/sellersAction';
import FaAngleDoubleRight from 'react-icons/lib/fa/angle-double-right';
import FaSearch from 'react-icons/lib/fa/search';

/* shouldComponentUpdate sat behind connect, which already refused to re-render
   this component unless the seller list had changed - between them the parent
   re-rendering on a route change did not reach it.  It takes no props, so memo
   is the same refusal, and its own state and store subscription still get it
   re-rendered when they should. */
const SidebarNav = React.memo(function SidebarNav() {
  const sellers = useSelector(state => state.getIn(['b', 'sellers']));
  const dispatch = useDispatch();

  // findDOMNode(this.refs.searchText) on a string ref to an <input> gave back
  // that input; a ref on the same input is the same node.
  const searchText = useRef(null);
  const [list, setList] = useState(Immutable.List([]));

  /* componentWillUpdate copied the incoming list straight onto the state object
     before rendering, so a new list from the store replaced whatever the search
     box had filtered down to.  Connect only re-rendered this component when that
     list actually changed, so "before every render" and "when the list changes"
     were the same moment; adjusting the state during the render that first sees
     a new list is that moment written out. */
  const [seen, setSeen] = useState(sellers);
  if (seen !== sellers) {
    setSeen(sellers);
    setList(sellers);
  }

  useEffect(() => {
    dispatch(sellersAction.sellersListInit());
        // console.log('componentDidMount...')
    scrollTo(0, 0);
        // debugger
        // this.props.sellersInit();
  }, [dispatch]);
    // componentWillReceiveProps(nextProps,nextState){
        // console.log('componentWillReceiveProps..');
        // debugger;
        /* 获取商家名称，要是一个数组或对象*/
        // debugger;
        // this.state.sellers=this.props.b.sellers;
        /**
         * 有一个bug，或者是和别的路由相同接口引起的。this.props.b.sellers是 []
         */
         // debugger;
        // this.setState({
        //     sellers:this.props.b.sellers
        // })
// debugger
        // console.log(nextProps.sellers);
        // bug消除：全部商家列表中，搜索后为部分商家，选择某商家，商家列表就回到全部商家。下面entireList函数，点击全部商家回到全部
        // if(nextProps.sellers){
        //     this.setState({
        //         sellers:nextProps.sellers
        //     })
        //     // this.change();
        // }
        // console.log(this)
        // console.log('输出sellers：')
        // console.log(this.props.b.sellers)
        // console.log(this.state.sellers)
        // this.state.sellers=['111','222','333','113','美宜佳','麦克风'];
    // }
  const change = useCallback(() => {
    const text = searchText.current.value;
        // if(text === ' ') return;  //输入中文时有空格
    const rows = [];
        // 输入时留下匹配的商家列表
        // let afdsa=sellers;
        // let css=sellers.toJS();
    sellers.forEach((seller) => {
            // seller是数值，不能indexOf，要转成string
      if (seller.indexOf(text.trim()) === -1) {

      } else {
        rows.push(seller);
      }
    });
    setList(Immutable.List(rows));
  }, [sellers]);
    // 回到整个商家列表
  const entireList = useCallback(() => {
        // let dom=searchText.current;
        // dom.value='';
    setList(sellers);
  }, [sellers]);

        // debugger
        // console.log('----this')
        // const usersdom = sellers.map(seller=><li>{seller.name}</li>);
        // console.log(list.toJS())
  const rows = [];
  let routeData = 'sellers/';
  rows.push(<li key="all" onClick={entireList}><Link to="sellers/allsellers" activeClassName="active" draggable="false">全部商家<FaAngleDoubleRight className="fa fa-angle-double-right" /></Link></li>);
  if (list) {
    list.forEach((seller, index) => {
      routeData += seller;
      rows.push(<li key={seller}><Link to={routeData} activeClassName="active" draggable="false">{seller}<FaAngleDoubleRight className="fa fa-angle-double-right" /></Link></li>);
      routeData = 'sellers/';
    });
  }
        // return <ul className="sidebar_nav">

        //    {usersdom}
        // </ul>
  return (<div id="sidebar_nav">
    <ul>
      <li key="search">
        <input type="text" ref={searchText} placeholder="搜索商家" onChange={change} /><FaSearch className="fa-search" />
      </li>
      {rows}
    </ul>
  </div>);
});

function Sellers({children}) {
  return (<div id="container">
    <SidebarNav />
    {children}
  </div>);
}

export default Sellers;

