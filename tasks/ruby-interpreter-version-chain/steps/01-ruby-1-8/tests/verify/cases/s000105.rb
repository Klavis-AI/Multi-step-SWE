class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
module BasicObjectSpecs
  class BOSubclass < BasicObject
    def self.kernel_defined?
      defined?(Kernel)
    end

    include ::Kernel
  end
end

p(Object.constants(false))