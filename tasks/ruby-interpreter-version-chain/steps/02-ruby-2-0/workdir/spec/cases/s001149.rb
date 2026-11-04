class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
enum = Enumerator.new do |yielder|
        "a\nb\nc".each_line(&yielder)
      end

      p(enum.to_a)