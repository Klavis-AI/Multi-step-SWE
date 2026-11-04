# frozen_string_literal: false
obj = Object.new
    obj.instance_variable_set(:@a, 1)
    obj.freeze
    begin; obj.instance_variable_set(:@b, 1)
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end