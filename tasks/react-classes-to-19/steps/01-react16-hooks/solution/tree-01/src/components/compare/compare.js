
import React, {useCallback, useEffect, useRef} from 'react';
// import {Table } from 'react-bootstrap';
// import echarts from 'echarts';
// import lineAction from '../actions/lineAction';
// import FirstPageLineReducer from '../reducers/firstPageLineReducer';
import { Link } from 'react-router';
import FaExchange from 'react-icons/lib/fa/exchange';

// var echarts = require('echarts/lib/echarts'); //必须
// require('echarts/lib/chart/pie'); //图表类型
// require('echarts/lib/component/title'); //标题插件


// Never touched anything on the instance, so it does not have to be inside the
// component at all.
function removeSubActive(Dom) {
  const subDom = Dom.getElementsByTagName('ul')[0];
  subDom.className = subDom.className.replace(' active', '');
}

function Compare({children}) {
  // findDOMNode(this.refs.split1) on a string ref to a host element gave back
  // the <li> itself; a ref on that same <li> is the same node.  The third string
  // ref, sidebar_subNav, was never read - the class reached that <ul> through
  // getElementsByClassName off split2 - so it has nothing to become.
  const split1 = useRef(null);
  const split2 = useRef(null);

  useEffect(() => {
    scrollTo(0, 0);
  }, []);

  const changeActive = useCallback((e) => {
    let dom = e.target;
    const domClass = e.target.className;
    const domTag = e.target.tagName;
         // 点击的不是父li，要转换到父li
    if (domClass !== 'split') {
            // 点击的目标是a或i，取父级的父级:ul
      if (domTag === 'A' || domTag === 'I') {
        dom = e.target.parentNode.parentNode.parentNode;
      } else if (domTag === 'LI') {
        dom = e.target.parentNode.parentNode;
      }
    }

    const subDom = dom.getElementsByTagName('ul')[0];
        // 子组件还没渲染出来，直接返回，防止子ul的active类被移除。
    if (!subDom) { return; }
        // 目标li已激活，返回
    if (subDom && subDom.className.indexOf('active') !== -1) {
      return;
    }
    removeSubActive(split1.current);
    removeSubActive(split2.current);
        // console.log(subDom)
    subDom.className = `${subDom.className} active`;
  }, []);

  // Bound in the constructor, so the child that receives it through
  // cloneElement saw one function for the life of the route; refs are stable,
  // so an empty dependency list keeps that.
  const changeActiveRoute = useCallback(() => {
    removeSubActive(split1.current);
    removeSubActive(split2.current);
    const dom2 = split2.current.getElementsByClassName('sidebar_subNav')[0];
    dom2.className = `${dom2.className} active`;
  }, []);

  return (<div id="container">
    <div id="sidebar_nav">
      <ul>
        <li className="split" ref={split1} onClick={changeActive}>&nbsp;<FaExchange />&nbsp;整体对比
                  <ul className="sidebar_subNav active">
                    <li><Link to="compare/wholeCustomerAvg" activeClassName="active" draggable="false">客流量对比</Link></li>
                    <li><Link to="compare/wholeCustomerNum" activeClassName="active" draggable="false">客流量峰值对比</Link></li>
                    <li><Link to="compare/wholeCustomerIn" activeClassName="active" draggable="false">入店量对比</Link></li>
                    <li><Link to="compare/wholeOldOrNew" activeClassName="active" draggable="false">新老顾客对比</Link></li>
                    <li><Link to="compare/wholeStay" activeClassName="active" draggable="false">停留时长对比</Link></li>
                    <li><Link to="compare/wholeDeep" activeClassName="active" draggable="false"><i className="fa fa-tachometer" aria-hidden="true" />深访率对比<i className="fa fa-angle-double-right" aria-hidden="true" /></Link></li>
                    <li><Link to="compare/wholeOut" activeClassName="active" draggable="false"><i className="fa fa-tachometer" aria-hidden="true" />跳出率对比<i className="fa fa-angle-double-right" aria-hidden="true" /></Link></li>
                    <li><Link to="compare/wholeCycle" activeClassName="active" draggable="false">来访周期对比</Link></li>
                    <li><Link to="compare/wholeActive" activeClassName="active" draggable="false">活跃度对比</Link></li>
                  </ul>
        </li>
        <li className="split" ref={split2} onClick={changeActive}>&nbsp;<FaExchange />&nbsp;商家对比
                  <ul className="sidebar_subNav">
                    <li><Link to="compare/sellerCustomerAvg" activeClassName="active" draggable="false">客流量对比</Link></li>
                    <li><Link to="compare/sellerCustomerNum" activeClassName="active" draggable="false">客流量峰值对比</Link></li>
                    <li><Link to="compare/sellerCustomerIn" activeClassName="active" draggable="false">入店量对比</Link></li>
                    <li><Link to="compare/sellerOldOrNew" activeClassName="active" draggable="false">新老顾客对比</Link></li>
                    <li><Link to="compare/sellerDeep" activeClassName="active" draggable="false">深访率对比</Link></li>
                    <li><Link to="compare/sellerOut" activeClassName="active" draggable="false">跳出率对比</Link></li>
                    <li><Link to="compare/sellerStay" activeClassName="active" draggable="false">驻店时长对比</Link></li>
                    <li><Link to="compare/sellerActive" activeClassName="active" draggable="false">活跃度对比</Link></li>
                    {/* <li><Link to='compare/sellerTimeSection' activeClassName="active" draggable="false">各时间段人数对比</Link></li>*/}
                    <li><Link to="compare/sellerCycle" activeClassName="active" draggable="false">来访周期对比</Link></li>
                  </ul>
        </li>
      </ul>
    </div>
    {children && React.cloneElement(children, {changeActiveRoute})}
  </div>);
}
// let Compare=connect(state=>state.toJS(),null)(_compare);

export default Compare;
