$LOAD_PATH.unshift("/app/gems/lib")
require "abbrev"
p Abbrev.abbrev(%w[apple apply], "app").keys.sort
