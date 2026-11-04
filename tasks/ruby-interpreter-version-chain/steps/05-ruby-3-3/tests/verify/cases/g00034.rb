$LOAD_PATH.unshift("/app/gems/lib")
require "shellwords"
p Shellwords.escape("three blind mice")
