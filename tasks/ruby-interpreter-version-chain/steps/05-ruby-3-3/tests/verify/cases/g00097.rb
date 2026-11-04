$LOAD_PATH.unshift("/app/gems/lib")
require "abbrev"
h = Abbrev.abbrev(%w[x y z])
p h.keys.sort
p h.values.uniq.sort
