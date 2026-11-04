$LOAD_PATH.unshift("/app/gems/lib")
require "abbrev"
p Abbrev.abbrev(%w[]).size
