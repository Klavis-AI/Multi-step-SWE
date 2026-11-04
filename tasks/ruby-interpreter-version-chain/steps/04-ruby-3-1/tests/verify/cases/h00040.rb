begin
  f = Fiber.new { Fiber.yield 1; 2 }
  p f.alive?
  p f.resume
  p f.alive?
  p f.resume
  p f.alive?
rescue Exception => e
  p e.class
end
