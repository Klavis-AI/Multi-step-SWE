class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
module ArraySpec
  class Splat
    def unpack_3args(a, b, c)
      [a, b, c]
    end

    def unpack_4args(a, b, c, d)
      [a, b, c, d]
    end
  end

  class SideEffect
    def initialize()
      @call_count = 0
    end

    attr_reader :call_count

    def array_result(a_number)
      [result(a_number), result(a_number)]
    end

    def result(a_number)
      @call_count += 1
      if a_number
        1
      else
        :thing
      end
    end
  end
end

p(%w(a  b c  d))
    p(%W(hello
       world))