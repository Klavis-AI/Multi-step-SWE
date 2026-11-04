$LOAD_PATH.unshift("/app/gems/lib")
require "colorize"
String.disable_colorization = true
p "t".red
String.disable_colorization = false
p "t".red
