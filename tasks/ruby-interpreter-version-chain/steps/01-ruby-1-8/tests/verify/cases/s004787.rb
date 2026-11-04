class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
object = Object.new
      class << object
        def []=(key, v)
          @h ||= {}
          @h[key] = v
        end

        def [](key)
          (@h || {})[key]
        end

        def assign(k1, v1, k2, v2)
          (self[k1], self[k2]), c = [v1, v2], nil
        end
      end

      object.assign :k1, :v1, :k2, :v2
      p(object[:k1])
      p(object[:k2])