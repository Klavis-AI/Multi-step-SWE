class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
p(case
    when false; 'foo'
    when 2; 'bar'
    when 1 == 1; 'baz'
    end)

    p(case
    when false; 'foo'
    when nil; 'foo'
    when 1 == 1; 'bar'
    end)