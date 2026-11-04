
import React, {useCallback, useState} from 'react';
import PropTypes from 'prop-types';
import { Link } from 'react-router';

function Chart({params, children}) {
  const [sellerName, setSellerName] = useState('');

  // The class bound this once in the constructor, so the child saw the same
  // function for the life of the route; an empty dependency list says the same.
  const changeSellerName = useCallback((name) => {
    setSellerName(name);
  }, []);

  const nowUrl = `/sellers/${params.id}/now`;
  const historyUrl = `/sellers/${params.id}/history`;
  return (<div className="chartWrapper">
    <div className="singleSellerRoute">
      <ul>
        <li><Link to={nowUrl} activeClassName="active" draggable="false">当前客流</Link></li>
        <li><Link to={historyUrl} activeClassName="active" draggable="false">历史数据</Link></li>
      </ul>
      <p>{sellerName}</p>
    </div>
    {children && React.cloneElement(children, {changeSellerName})}
  </div>);
}

Chart.propTypes = {
  params: PropTypes.object.isRequired       //
};

export default Chart;
