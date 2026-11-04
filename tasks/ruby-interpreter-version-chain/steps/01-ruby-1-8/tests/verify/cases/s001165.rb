class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
o = Object.new
    def o.each
      yield :a
      yield :b1, :b2
      yield :c
      yield :d1, :d2
      yield :e1, :e2, :e3
      yield nil
      yield
      yield [:f1, :f2]
    end

    @e = o.to_enum
p(@e.next)
    p(@e.next_values)
    p(@e.next)