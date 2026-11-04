class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
@klass = Struct.new(:a)
    @instance = @klass.new(@klass.new({ b: [1, 2, 3] }))
instance = Struct.new(:a, :b).new(:one, :two)
    p(instance.dig(0))
    p(instance.dig(1))