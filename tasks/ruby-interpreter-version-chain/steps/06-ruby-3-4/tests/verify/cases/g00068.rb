$LOAD_PATH.unshift("/app/gems/lib")
require "colorize"
p "t".colorize(:color => :red, :mode => :italic)
