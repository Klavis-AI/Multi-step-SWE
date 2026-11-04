# frozen_string_literal: false
begin; raise Interrupt.new
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end