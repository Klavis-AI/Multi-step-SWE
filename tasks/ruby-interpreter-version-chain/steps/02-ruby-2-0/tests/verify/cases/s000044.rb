class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
ary = []
      result = [1,2,3,4,5].max {|x| ary << x; x}

      p(ary)
      p(result)