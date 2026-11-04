$LOAD_PATH.unshift("/app/gems/lib")
require "shellwords"
p Shellwords.escape("  spaced   out  ")
