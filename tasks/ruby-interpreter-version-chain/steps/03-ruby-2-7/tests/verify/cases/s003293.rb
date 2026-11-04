class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
proc = proc {|_, _, _ = 1, *_, _:, _: 2, **_, &_| }
    p(proc.parameters)

    lambda = -> _, _, _ = 1, *_, _:, _: 2, **_, &_ {}
    p(lambda.parameters)