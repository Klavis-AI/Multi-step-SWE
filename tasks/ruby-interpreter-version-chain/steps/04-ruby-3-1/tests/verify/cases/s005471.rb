class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
module ForSpecs
  class ForInClassMethod
    m = :same_variable_set_outside

    def self.foo
      all = []
      for m in [:bar, :baz]
        all << m
      end
      all
    end

    READER = -> { m }
  end
end

class OFor
      m = [1,2,3]
      n = 0
      for @@var in m
        n += 1
      end
      p(@@var)
      p(n)
    end