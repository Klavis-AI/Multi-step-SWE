class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
c = true
    a = [1, 2]
    p(while c
      a[1] += begin
        break if c
        c = false
      end
    end)
    p(a)