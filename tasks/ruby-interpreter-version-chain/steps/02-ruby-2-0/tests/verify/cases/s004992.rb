class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
p(case
    when true, false; 'foo'
    else 'bar'
    end)

    p(case
    when false, true; 'foo'
    else 'bar'
    end)