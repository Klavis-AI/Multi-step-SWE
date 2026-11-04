class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
m = Module.new
      m.module_exec do
        self::A, self::B = :v1, :v2
      end

      p(m::A)
      p(m::B)