begin
  f = Fiber.new { Fiber.yield(1); Fiber.yield(2); Fiber.yield(3); Fiber.yield(4); Fiber.yield(5); :done }
  p f.resume
  p f.resume
  p f.resume
  p f.resume
  p f.resume
  p f.resume
rescue Exception => e
  p e.class
end
