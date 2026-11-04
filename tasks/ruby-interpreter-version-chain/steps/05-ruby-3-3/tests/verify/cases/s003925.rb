class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
@set = Set[1, 2, 3]
ret = []
    @set.each { |x| ret << x }
    p(ret.sort)