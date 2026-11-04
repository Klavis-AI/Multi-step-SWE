Bug4438 = '[ruby-core:35364]'
begin; begin
        raise
      rescue 1
      end
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end