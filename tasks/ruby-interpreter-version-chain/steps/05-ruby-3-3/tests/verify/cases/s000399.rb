class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
module DataSpecs
  if Data.respond_to?(:define)
    Measure = Data.define(:amount, :unit)
    Single = Data.define(:value)

    class MeasureWithOverriddenName < Measure
      def self.name
        "A"
      end
    end

    class SingleWithOverriddenName < Single
      def self.name
        "A"
      end
    end

    class DataSubclass < Data; end

    MeasureSubclass = Class.new(Measure) do
      def initialize(amount:, unit:)
        super
      end
    end

    Empty = Data.define()

    DataWithOverriddenInitialize = Data.define(:amount, :unit) do
      def initialize(*rest, **kw)
        super
        ScratchPad.record [:initialize, rest, kw]
      end
    end

    Area = Data.define(:width, :height, :area) do
      def initialize(width:, height:)
        super(width: width, height: height, area: width * height)
      end
    end
  end
end

data = DataSpecs::Measure[42, "km"]

    p(data.amount)
    p(data.unit)