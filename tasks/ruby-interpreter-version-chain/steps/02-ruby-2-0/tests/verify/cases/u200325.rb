Bug4438 = '[ruby-core:35364]'
string = "exception in ensure clause"
    e = begin; begin
        raise "this must be handled no.4"
      ensure
        assert_instance_of(RuntimeError, $!)
        assert_equal("this must be handled no.4", $!.message)
        raise "exception in ensure clause"
      end
      assert(false)
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end
    p(e.message)