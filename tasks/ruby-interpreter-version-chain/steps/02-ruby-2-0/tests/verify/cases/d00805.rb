begin
  f = Fiber.new { Fiber.yield(1); :done }
  p f.resume
  p f.resume
rescue Exception => e
  p e.class
end
