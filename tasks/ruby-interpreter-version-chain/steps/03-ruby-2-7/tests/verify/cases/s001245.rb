class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
base_size = 100
    enum = Enumerator.new(-> { base_size + 1 }) {}
    base_size = 200
    p(enum.size)
    base_size = 300
    p(enum.size)