$LOAD_PATH.unshift("/app/gems/lib")
require "abbrev"
p Abbrev.abbrev(%w[ruby rules]).sort.first(8)
