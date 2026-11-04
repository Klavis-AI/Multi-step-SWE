$LOAD_PATH.unshift("/app/gems/lib")
require "shellwords"
p Shellwords.join(Shellwords.split(""))
