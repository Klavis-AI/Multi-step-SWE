class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
pick = -> x {
      case x
      when 1267650600228229401496703205376 then :beyond_long
      when 10_000_000_000                  then :beyond_int
      when 1                               then :fits_int
      else                                      :other
      end
    }

    p([1267650600228229401496703205376, 10_000_000_000, 1, :nope].map(&pick))