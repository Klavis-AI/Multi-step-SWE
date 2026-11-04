import React, {useEffect} from 'react';
import { Link } from 'react-router';
import '../styles/firstPage.scss';

// path.dirname(__filename) + '../../lib/images/logo.50.png'.  webpack 2's
// node.__filename mock made __filename '/index.js', so dirname was '/' and the
// browser was asked for /../../lib/images/logo.50.png, which it resolved to
// /lib/images/logo.50.png.  Nothing outside a bundler defines __filename, and
// the image is served from public/ at a fixed place, so write the place down.
const imgSrc = '/lib/images/logo.50.png';

function General({children}) {
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
        <li><Link to="/firstPage" activeClassName="active" draggable="false">产品介绍</Link></li>
        <li><Link to="/statistics" activeClassName="active" draggable="false">商城统计</Link></li>
        <li><Link to="/sellers" activeClassName="active" draggable="false">商家统计</Link></li>
        <li><Link to="/compare" activeClassName="active" draggable="false">数据对比</Link></li>
      </ul>
    </nav>

    {children}

  </div>);
}
		// <DevTool/>


export default General;
