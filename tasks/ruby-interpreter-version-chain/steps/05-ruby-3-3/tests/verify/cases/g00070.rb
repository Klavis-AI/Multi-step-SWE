$LOAD_PATH.unshift("/app/gems/lib")
require "colorize"
p String.colors.size
p String.modes.sort.first(3)
