class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
def m(**kw)
        kw
      end

      empty = {}
      p(m(**empty))
      p(m(**empty))

      h = {a: 1}
      p(m(**h))
      p(m(**h))