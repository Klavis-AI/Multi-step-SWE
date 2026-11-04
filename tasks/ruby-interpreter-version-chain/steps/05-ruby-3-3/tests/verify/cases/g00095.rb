$LOAD_PATH.unshift("/app/gems/lib")
require "abbrev"
h = Abbrev.abbrev(%w[alpha beta])
p h.keys.sort
p h.values.uniq.sort
