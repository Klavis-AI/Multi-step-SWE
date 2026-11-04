class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
def m(*args)
        args[0]
      end

      h = {a: 1}
      p(m(**h))
      p(h)