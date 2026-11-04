class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
@p = proc { |*a, **kw| [a, kw] }
p(@p.call([1, {a: 1}]))