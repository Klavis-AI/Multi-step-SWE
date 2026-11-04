class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
ScratchPad.record []
p(case [0, {a: 42, b: [0, 1]}, {a: 42, b: [1, 2]}]
      in [*, {a:, b: [1, c]}, *]
        [a, c]
      else
        false
      end)