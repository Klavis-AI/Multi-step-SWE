# frozen_string_literal: false
begin; Fiber.new do
        1/0
      end.resume
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end
    begin; fib = Fiber.new{ Fiber.yield }
      fib.resume
      fib.raise "raise and propagate"
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end
    begin; fib = Fiber.new do
        begin
          Fiber.yield
        rescue
        end
      end
      fib.resume
      fib.raise "rescue in fiber"
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end
    fib = Fiber.new do
      begin
        Fiber.yield
      rescue
        Fiber.yield :ok
      end
    end
    fib.resume
    p(fib.raise)