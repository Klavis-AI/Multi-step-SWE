$LOAD_PATH.unshift("/app/gems/lib")
require "colorize"
s = "t".red
p s.uncolorize
p s.colorized?
p "plain".colorized?
