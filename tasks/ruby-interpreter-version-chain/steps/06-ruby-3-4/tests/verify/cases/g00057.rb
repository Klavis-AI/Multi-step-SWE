$LOAD_PATH.unshift("/app/gems/lib")
require "shellwords"
p "a b".shellsplit
p "a b".shellescape
