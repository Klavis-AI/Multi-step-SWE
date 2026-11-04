class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
klass = Class.new do
          def [](k)
            @hash ||= {}
            @hash[k]
          end

          def []=(k, v)
            @hash ||= {}
            @hash[k] = v
            7
          end
        end
        @b = klass.new
@b[:m] = 10
          p((@b[*[:m]] += 10))
          p(@b[:m])

          @b[:n] = 10
          p((@b[*(1; [:n])] += 10))
          p(@b[:n])

          @b[:k] = 10
          p((@b[*begin 1; [:k] end] += 10))
          p(@b[:k])