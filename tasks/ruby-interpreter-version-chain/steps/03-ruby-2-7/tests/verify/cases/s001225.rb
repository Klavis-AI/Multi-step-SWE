class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
enum = Enumerator.product(1..2)
    p(enum.to_a)

    enum = Enumerator.product(1..2, ["A"])
    p(enum.to_a)

    enum = Enumerator.product(1..2, ["A"], ["B"])
    p(enum.to_a)

    enum = Enumerator.product(2..3, ["A"], ["B"], ["C"])
    p(enum.to_a)