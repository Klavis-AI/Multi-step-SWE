class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
object = Object.new
    object.freeze

    msg_class = ruby_version_is("4.0") ? "Object" : "object"

    p(-> {
      def object.x; end
    })

    object = [].freeze
    p(-> { object << nil })