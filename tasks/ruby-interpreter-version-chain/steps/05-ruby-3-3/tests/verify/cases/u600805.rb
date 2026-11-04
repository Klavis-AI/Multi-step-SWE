# frozen_string_literal: false
class Step
    include Enumerable
    attr_reader :current, :args

    def initialize(enum)
      @enum = enum
      @current = nil
      @args = nil
    end

    def each(*args)
      @args = args
      @enum.each do |v|
        @current = v
        if v.is_a? Enumerable
          yield(*v)
        else
          yield(v)
        end
      end
    end
  end
a = Step.new(1..3)
    p(a.flat_map {|x| [x * 2]}.first)
    p(a.current)
    p(a.lazy.flat_map {|x| [x * 2]}.first)
    p(a.current)