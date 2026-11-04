class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
# -*- encoding: us-ascii -*-

module EnumeratorLazySpecs
  class SpecificError < Exception; end

  class YieldsMixed
    def self.initial_yields
      [nil, 0, 0, 0, 0, nil, :default_arg, [], [], [0], [0, 1], [0, 1, 2]]
    end

    def self.gathered_yields
      [nil, 0, [0, 1], [0, 1, 2], [0, 1, 2], nil, :default_arg, [], [], [0], [0, 1], [0, 1, 2]]
    end

    def self.gathered_non_array_yields
      [nil, 0, nil, :default_arg]
    end

    def self.gathered_yields_with_args(arg, *args)
      [nil, 0, [0, 1], [0, 1, 2], [0, 1, 2], nil, arg, args, [], [0], [0, 1], [0, 1, 2]]
    end

    def each(arg=:default_arg, *args)
      yield
      yield 0
      yield 0, 1
      yield 0, 1, 2
      yield(*[0, 1, 2])
      yield nil
      yield arg
      yield args
      yield []
      yield [0]
      yield [0, 1]
      yield [0, 1, 2]
    end
  end

  class EventsMixed
    def each
      ScratchPad << :before_yield

      yield 0

      ScratchPad << :after_yield

      raise SpecificError

      ScratchPad << :after_error

      :should_not_reach_here
    end
  end
end

@yieldsmixed = EnumeratorLazySpecs::YieldsMixed.new.to_enum.lazy
    @eventsmixed = EnumeratorLazySpecs::EventsMixed.new.to_enum.lazy
    ScratchPad.record []
s = Enumerator.new do |y| loop do y << [1, 2] end end

    expected = s.take(3).flat_map { |x| x }.to_a
    actual = s.lazy.take(3).flat_map{ |x| x }.force
    p(actual)