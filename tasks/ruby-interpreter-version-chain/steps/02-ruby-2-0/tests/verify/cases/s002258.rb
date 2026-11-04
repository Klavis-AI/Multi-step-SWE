class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
a = [false]
    b = 1.times do |i|
      while true
        a.shift or break
      end
    end
    p(a)
    p(b)