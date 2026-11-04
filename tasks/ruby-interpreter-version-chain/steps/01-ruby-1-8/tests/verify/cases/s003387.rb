class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
p((0..1).respond_to?(:hash))
    p(('A'..'Z').respond_to?(:hash))
    p((0xfffd..0xffff).respond_to?(:hash))
    p((0.5..2.4).respond_to?(:hash))