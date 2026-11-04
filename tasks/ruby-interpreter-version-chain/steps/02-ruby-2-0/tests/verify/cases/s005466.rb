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

for i, *j in [[1,2]]
      p(i)
      p(j)
    end