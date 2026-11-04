class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
def m(**kw) kw; end

      h = nil
      p(m(**h))
      p(m(a: 1, **h))

      p(m(**nil))
      p(m(a: 1, **nil))