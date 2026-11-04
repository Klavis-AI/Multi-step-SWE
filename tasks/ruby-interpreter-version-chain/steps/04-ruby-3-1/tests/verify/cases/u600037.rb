# coding: US-ASCII
# frozen_string_literal: false
@verbose = $VERBOSE
    @cls = Class.new(Array)
p(@cls[1,2,3].uniq.class)
    p(@cls[1,2][0,1].class) # embedded
    p(@cls[*(1..100)][1..99].class)