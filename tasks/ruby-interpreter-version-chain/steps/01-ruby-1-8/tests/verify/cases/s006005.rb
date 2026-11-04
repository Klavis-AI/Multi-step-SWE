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
      begin
        list << 10*x
        redo if list.count(1) == 1
      ensure
        list << 100*x
      end
    end
    p(list)