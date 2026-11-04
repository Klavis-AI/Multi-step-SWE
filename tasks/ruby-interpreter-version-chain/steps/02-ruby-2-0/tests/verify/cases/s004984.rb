class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
a1 = ['f', 'o', 'o']
    a2 = ['b', 'a', 'r']

    p(case 'f'
    when *a1, *['x', 'y', 'z']
      "foo"
    when *a2, *['x', 'y', 'z']
      "bar"
    end)

    p(case 'b'
    when *a1, *['x', 'y', 'z']
      "foo"
    when *a2, *['x', 'y', 'z']
      "bar"
    end)