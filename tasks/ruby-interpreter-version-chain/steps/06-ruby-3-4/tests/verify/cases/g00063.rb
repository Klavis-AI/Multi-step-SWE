$LOAD_PATH.unshift("/app/gems/lib")
require "colorize"
p "text".send(:light_green.to_s)
