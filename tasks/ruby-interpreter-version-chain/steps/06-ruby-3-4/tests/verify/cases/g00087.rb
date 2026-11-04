$LOAD_PATH.unshift("/app/gems/lib")
require "colorize"
s = "abc".red
p s
p s.uncolorize
p s.length
