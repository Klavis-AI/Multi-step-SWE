class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
a, b, c = :'foo', :'+', :'Foo__9'
    p(a)
    p(a.inspect)
    p(b)
    p(b.inspect)
    p(c)
    p(c.inspect)