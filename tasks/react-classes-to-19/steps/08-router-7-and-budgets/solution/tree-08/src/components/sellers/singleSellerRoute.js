
import React, {useState} from 'react';
import ActiveLink from '../activeLink';
import { Outlet, useParams } from 'react-router';

function Chart() {
  const params = useParams();
  const [sellerName, setSellerName] = useState('');

  // The class bound this once in the constructor, so the child saw the same
  // function for the life of the route; an empty dependency list says the same.
  const changeSellerName = (name) => {
    setSellerName(name);
  };

  const nowUrl = `/sellers/${params.id}/now`;
  const historyUrl = `/sellers/${params.id}/history`;
  return (<div className="chartWrapper">
    <div className="singleSellerRoute">
      <ul>
        <li><ActiveLink to={nowUrl} draggable="false">当前客流</ActiveLink></li>
        <li><ActiveLink to={historyUrl} draggable="false">历史数据</ActiveLink></li>
      </ul>
      <p>{sellerName}</p>
    </div>
    <Outlet context={{changeSellerName}} />
  </div>);
}

export default Chart;
