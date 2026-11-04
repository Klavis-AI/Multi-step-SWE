Bug4438 = '[ruby-core:35364]'
string = "this must be handled no.3"
    e = begin; begin
        raise "exception in rescue clause"
      rescue
        raise string
      end
      assert(false)
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end
    p(e.message)