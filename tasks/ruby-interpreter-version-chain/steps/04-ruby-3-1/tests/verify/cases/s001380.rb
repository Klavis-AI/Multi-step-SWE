class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
step = 0
    fiber = Fiber.new { step = 1; Fiber.yield; step = 2; Fiber.yield; step = 3 }
    fiber.resume
    p(step)
    fiber.resume
    p(step)