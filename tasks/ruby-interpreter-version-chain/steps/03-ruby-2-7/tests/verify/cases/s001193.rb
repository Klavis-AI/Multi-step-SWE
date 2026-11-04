class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
enum = Enumerator.produce(0, size: 10) { |n| n + 1 }

        p(enum.size)
        p(enum.take(5))