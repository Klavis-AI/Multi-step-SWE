$LOAD_PATH.unshift("/app/gems/lib")
require "colorize"
s = "abc".light_blue
p s
p s.uncolorize
p s.length
