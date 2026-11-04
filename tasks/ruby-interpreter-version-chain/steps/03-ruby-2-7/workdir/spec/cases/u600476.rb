# frozen_string_literal: false
str = "str"
    str.freeze
    begin; str.instance_variable_set(:@a, 1)
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end