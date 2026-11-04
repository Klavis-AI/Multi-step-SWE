Bug4438 = '[ruby-core:35364]'
cmd = proc{raise SystemExit}
    safe0_p = proc{|*args| args}

    test_proc = proc {
      $SAFE = 4
      begin
        cmd.call
      rescue SystemExit => e
        safe0_p["SystemExit: #{e.inspect}"]
        raise e
      rescue Exception => e
        safe0_p["Exception (NOT SystemExit): #{e.inspect}"]
        raise e
      end
    }
    begin; test_proc.call
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end