class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
object = Object.new
    class << object
      attr_accessor :a, :b
    end

    (object.a, object.b), c = [:a, :b], nil

    p(object.a)
    p(object.b)