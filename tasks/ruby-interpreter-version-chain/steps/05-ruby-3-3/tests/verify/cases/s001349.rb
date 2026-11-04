class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
o = Object.new
    def o.f
      a = 1
      f = Fiber.new { a = 2 }
      f.resume
      a
    end
    p(o.f)