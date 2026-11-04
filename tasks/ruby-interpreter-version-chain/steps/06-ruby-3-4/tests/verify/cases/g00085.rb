$LOAD_PATH.unshift("/app/gems/lib")
require "shellwords"
v = Shellwords.split("x'y'z")
p v
p Shellwords.join(v)
p Shellwords.split(Shellwords.join(v)) == v
