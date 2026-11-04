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

obj = Object.new
    def obj.each
      (0..10).each { |i| yield i }
    end

    j = 0
    for i in obj
      j += i
    end
    p(j)