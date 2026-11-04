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
klass_with_multiple_parameters = Class.new do
            def [](k1, k2, k3)
              @hash ||= {}
              @hash[:"#{k1}#{k2}#{k3}"]
            end

            def []=(k1, k2, k3, v)
              @hash ||= {}
              @hash[:"#{k1}#{k2}#{k3}"] = v
              7
            end
          end
          a = klass_with_multiple_parameters.new

          a[:a, :b, :c] = 10
          p((a[*[:a], *[:b], *[:c]] += 10))
          p(a[:a, :b, :c])