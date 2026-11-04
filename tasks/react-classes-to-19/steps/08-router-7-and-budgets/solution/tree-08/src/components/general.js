import React, {useEffect} from 'react';
import ActiveLink from './activeLink';
import { Link, Outlet } from 'react-router';
import '../styles/firstPage.scss';

// path.dirname(__filename) + '../../lib/images/logo.50.png'.  webpack 2's
// node.__filename mock made __filename '/index.js', so dirname was '/' and the
// browser was asked for /../../lib/images/logo.50.png, which it resolved to
// /lib/images/logo.50.png.  Nothing outside a bundler defines __filename, and
// the image is served from public/ at a fixed place, so write the place down.
const imgSrc = '/lib/images/logo.50.png';

function General() {
  useEffect(() => {
    // 取消进入的动画
    setTimeout(() => {
      const round = document.getElementsByClassName('in-round')[0];
      if (round && round.style) {
        round.style.display = 'none';
      }
    }, 0);
  }, []);

  return (<div>
    <nav className="nav">
      <div className="brand"><Link to='/firstPage'><img src={imgSrc} /></Link></div>
      <ul className="navMenu">
        <li><ActiveLink to="/firstPage" draggable="false">产品介绍</ActiveLink></li>
        <li><ActiveLink to="/statistics" draggable="false">商城统计</ActiveLink></li>
        <li><ActiveLink to="/sellers" draggable="false">商家统计</ActiveLink></li>
        <li><ActiveLink to="/compare" draggable="false">数据对比</ActiveLink></li>
      </ul>
    </nav>

    <Outlet />

  </div>);
}
		// <DevTool/>


export default General;
