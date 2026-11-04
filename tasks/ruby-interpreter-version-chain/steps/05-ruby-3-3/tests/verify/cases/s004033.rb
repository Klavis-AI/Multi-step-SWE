class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end

module SetSpecs
  # This class is used to test the interaction of "Set-like" objects with real Sets
  #
  # These "Set-like" objects reply to is_a?(Set) with true and thus real Set objects are able to transparently
  # interoperate with them in a duck-typing manner.
  class SetLike
    include Enumerable

    def is_a?(klass)
      super || klass == ::Set
    end

    def initialize(entries)
      @entries = entries
    end

    def each(&block)
      @entries.each(&block)
    end

    def inspect
      "#<#{self.class}: {#{map(&:inspect).join(", ")}}>"
    end

    def size
      @entries.size
    end
  end
end

@set = Set[1, 2, 3, 4]
p(@set.superset?(@set))
    p(Set[].superset?(Set[]))

    p(@set.superset?(Set[]))
    p(Set[1, 2, 3].superset?(Set[]))
    p(Set["a", :b, ?c].superset?(Set[]))

    p(@set.superset?(Set[1, 2, 3]))
    p(@set.superset?(Set[1, 3]))
    p(@set.superset?(Set[1, 2]))
    p(@set.superset?(Set[1]))

    p(@set.superset?(Set[5]))
    p(@set.superset?(Set[1, 5]))
    p(@set.superset?(Set[nil]))
    p(@set.superset?(Set["test"]))