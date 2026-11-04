class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
@numbers = [
      29871.2722891,
      999.1**99.928888,
      -72628191273.22,
      29282.2827,
      -2927.00091,
      12.0,
      Float::MAX,
    ]
@numbers.each do |number|
      p(number.infinite?)
      p(number.numerator)
    end