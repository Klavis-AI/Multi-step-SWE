class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
object = Object.new
    class << object
      def []=(k, v) (@h ||= {})[k] = v; end
      def [](k) (@h ||= {})[k]; end
    end

    (object[:a], object[:b]), c = [:v1, :v2], nil

    p(object[:a])
    p(object[:b])