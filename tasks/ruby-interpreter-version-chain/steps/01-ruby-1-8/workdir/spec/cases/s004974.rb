class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
a = ['a', 'b', 'c', 'd']
    b = ['f']

    p(case 'f'
    when 'f', *a|b
      "foo"
    when *['x', 'y', 'z']
      "bar"
    end)