class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
module CoreClassSpecs
  class Record
  end

  module M
    def inherited(klass)
      ScratchPad.record klass
      super
    end
  end

  class F; end
  class << F
    include M
  end

  class A
    def self.inherited(klass)
      ScratchPad.record klass
    end
  end

  class H < A
    def self.inherited(klass)
      super
    end
  end

  module Inherited
    class A
      SUBCLASSES = []
      def self.inherited(subclass)
        SUBCLASSES << [self, subclass]
      end
    end

    class B < A; end
    class B < A; end # reopen
    class C < B; end

    class D
      def self.inherited(subclass)
        ScratchPad << self
      end
    end
  end
end

klass = Class.new
    p(klass.is_a?(Class))

    klass_instance = klass.new
    p(klass_instance.is_a?(klass))