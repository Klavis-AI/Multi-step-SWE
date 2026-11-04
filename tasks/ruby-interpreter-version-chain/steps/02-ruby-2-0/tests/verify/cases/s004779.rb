class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
object = Object.new
      def object.[]=(_, _) end
      ScratchPad.record []

      ((ScratchPad << :a; object)[(ScratchPad << :b; :b)], foo), bar = [(ScratchPad << :c; :c)]
      p(ScratchPad.recorded)