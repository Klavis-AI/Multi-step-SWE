class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
list = []
    [1,2,3].each do |x|
      list << x
      break if list.size == 6
      redo if x == 3
    end
    p(list)