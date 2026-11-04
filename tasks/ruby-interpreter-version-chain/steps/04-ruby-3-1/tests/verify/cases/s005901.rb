class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
ScratchPad.record []
obj = Object.new

      def obj.deconstruct_keys(*)
        ScratchPad << :deconstruct_keys
        {a: 1}
      end

      p(case obj
      in {b: 1}
        false
      in {a: 1}
        true
      end)

      p(ScratchPad.recorded)