/* Two ways of writing a value into slice state at a path, because the store
   this replaces had two.

   The old state was an Immutable collection, and `Map#setIn` returned the map
   *unchanged* when the value already at the path was value-equal to the one
   being written.  That is not an optimisation detail: a response identical to
   the one already in the store produced no new state, no re-render, and no
   `setOption`, which is what a screen does when it is re-entered and the server
   answers the same thing twice.  `setIn` below keeps that.

   Where the old reducer stored a plain JS array instead of an `Immutable.List`
   - the whole of compare, and 首页's seller bars - the same equality test was a
   reference test between two freshly built arrays, so it never held and the
   write always produced new state.  `replaceIn` keeps that.

   Which of the two a given write uses is therefore not a style choice; it is
   the behaviour of the screen that reads it. */

function sameValue(a, b) {
  if (a === b) return true;
  if (Array.isArray(a) && Array.isArray(b)) {
    if (a.length !== b.length) return false;
    for (let i = 0; i < a.length; i++) {
      if (!sameValue(a[i], b[i])) return false;
    }
    return true;
  }
  if (a && b && typeof a === 'object' && typeof b === 'object'
      && !Array.isArray(a) && !Array.isArray(b)) {
    const keys = Object.keys(a);
    if (keys.length !== Object.keys(b).length) return false;
    for (const k of keys) {
      if (!Object.prototype.hasOwnProperty.call(b, k)) return false;
      if (!sameValue(a[k], b[k])) return false;
    }
    return true;
  }
  return false;
}

function parentOf(state, path) {
  let node = state;
  for (let i = 0; i < path.length - 1; i++) {
    if (node[path[i]] === undefined || node[path[i]] === null) {
      node[path[i]] = {};
    }
    node = node[path[i]];
  }
  return node;
}

// 写入，值相同则不动（对应 Immutable 的 setIn）
export function setIn(state, path, value) {
  const leaf = path[path.length - 1];
  const parent = parentOf(state, path);
  if (!sameValue(parent[leaf], value)) {
    parent[leaf] = value;
  }
}

// 每次都写入新值（对应原来存普通数组的那些位置）
export function replaceIn(state, path, value) {
  parentOf(state, path)[path[path.length - 1]] = value;
}
