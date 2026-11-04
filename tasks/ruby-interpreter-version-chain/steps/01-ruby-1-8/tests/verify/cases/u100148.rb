begin
      raise "this must be handled"
      p(false)
    rescue
      p(true)
    end

    $bad = true
    begin
      raise "this must be handled no.2"
    rescue
      if $bad
        $bad = false
        retry
        p(false)
      end
    end
    p(true)

    # exception in rescue clause
    $string = "this must be handled no.3"
    e = begin; begin
        raise "exception in rescue clause"
      rescue
        raise $string
      end
      assert(false)
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end
    p(e.message)

    # exception in ensure clause
    $string = "exception in ensure clause"
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

    $bad = true
    begin
      begin
        raise "this must be handled no.5"
      ensure
        $bad = false
      end
    rescue
    end
    p(!$bad)

    $bad = true
    begin
      begin
        raise "this must be handled no.6"
      ensure
        $bad = false
      end
    rescue
    end
    p(!$bad)

    $bad = true
    while true
      begin
        break
      ensure
        $bad = false
      end
    end
    p(!$bad)

    p(catch(:foo) {
         loop do
           loop do
    	 throw :foo, true
    	 break
           end
           break
           assert(false)			# should no reach here
         end
         false
       })