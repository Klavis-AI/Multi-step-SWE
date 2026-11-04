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

        def assign(v1, v2)
          (self.a, self.b), c = [v1, v2], nil
        end
      end

      object.assign :v1, :v2
      p(object.a)
      p(object.b)