import React, {useEffect} from 'react';
import { IndexLink } from 'react-router';

import '../styles/notFindPage.scss';

function NotFindPage({params}) {
  // componentWillMount logged before the first render; an effect logs after it.
  // Nothing reads the console between the two, and the value logged is the same.
  useEffect(() => {
    console.log(params);
  }, []);
  return (<div className="notFindPage">
    <div className="errorMsg">
      <p>404</p>
      {/* 这是注释*/}
      <i className="fa fa-times" />未找到页面
      <p>您可以点击<IndexLink to="/">返回首页</IndexLink></p>
    </div>


  </div>);
}

export default NotFindPage;
