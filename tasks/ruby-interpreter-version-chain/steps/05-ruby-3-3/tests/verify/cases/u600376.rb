# frozen_string_literal: false
fib = Fiber.new{Fiber.yield}
    p(fib.alive?)
    fib.resume
    p(fib.alive?)
    fib.resume
    p(fib.alive?)