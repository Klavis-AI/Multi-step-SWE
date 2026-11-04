import React from 'react';
import { NavLink, useMatches } from 'react-router';

/* A link that marks itself current the way react-router 3's activeClassName did.
 *
 * NavLink decides from the address: a link to /statistics is current on
 * /statistics/nosuchchart, because the address begins with it.  react-router 3
 * decided from the routes that matched, and on an address no route matches -
 * where the 404 page is what renders - nothing was current.
 *
 * So the two are intersected: the link is current when the router considers it
 * current and the path it points at is one of the routes that matched.  The
 * second comparison ignores case for the same reason the router's own does:
 * /Statistics/total matches the /statistics/total route, and the link was
 * current then too.
 */
/* the address the router reports keeps the escapes the URL was written with,
   while a link is built out of a decoded id, so both sides are decoded before
   they are compared.  A trailing slash is dropped for the same reason: on
   /statistics/total/ the route that matched is the same route, and the link to
   /statistics/total was current, but the matched pathname the router reports
   carries the slash through and a raw string comparison would not agree. */
function decode(path) {
  let p;
  try {
    p = decodeURIComponent(path);
  } catch (e) {
    p = path;
  }
  p = p.toLowerCase();
  return p.length > 1 ? p.replace(/\/+$/, '') : p;
}

function ActiveLink({to, children, ...rest}) {
  const matches = useMatches();
  const want = decode(to);
  const routed = matches.some((m) => decode(m.pathname) === want);
  return (<NavLink
    to={to}
    aria-current={null}
    className={({isActive}) => (isActive && routed ? 'active' : undefined)}
    {...rest}
  >{children}</NavLink>);
}

export default ActiveLink;
