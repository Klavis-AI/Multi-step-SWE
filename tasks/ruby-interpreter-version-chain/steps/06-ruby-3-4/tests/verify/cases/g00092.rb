$LOAD_PATH.unshift("/app/gems/lib")
require "colorize"
s = "abc".magenta
p s
p s.uncolorize
p s.length
