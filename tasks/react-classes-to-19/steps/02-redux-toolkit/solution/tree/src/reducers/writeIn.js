/* Write a value into slice state at a path.

   The store this replaces held an Immutable collection, and every case in every
   reducer ended in a chain of `Map#setIn`.  That method looks like it might
   collapse a write of an equal value, and it does - but on *reference* identity,
   not on value: `updateIn` keeps the old collection only when the new value is
   `===` the one already at the path.  Every one of those reducers built its
   value fresh - `Immutable.List(times)`, `Immutable.fromJS(yesterday)`, or a
   plain JS array - so the test never held and the write always produced new
   state, even when the server had answered the same thing twice.  That is what
   a screen relies on when it is re-entered: the response arrives again, the
   slice changes again, and the chart is drawn again.

   The few places that wrote a string or a number instead of a collection - a
   seller's name, a last-page number, the radar's five indicator labels - did
   collapse, because two equal primitives are `===`.  Assigning through immer
   reproduces both halves for free: its setter skips the write when the value is
   already `Object.is`-equal to what is there, and marks the slice changed
   otherwise. */

function parentOf(state, path) {
  let node = state;
  for (let i = 0; i < path.length - 1; i++) {
    if (node[path[i]] === undefined || node[path[i]] === null) {
      node[path[i]] = typeof path[i + 1] === 'number' ? [] : {};
    }
    node = node[path[i]];
  }
  return node;
}

export function writeIn(state, path, value) {
  parentOf(state, path)[path[path.length - 1]] = value;
}
