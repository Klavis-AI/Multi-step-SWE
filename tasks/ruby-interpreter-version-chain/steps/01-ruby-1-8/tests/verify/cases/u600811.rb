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
a = Step.new(1..6)
    p(a.reject {|x| x < 4}.first)
    p(a.current)
    p(a.lazy.reject {|x| x < 4}.first)
    p(a.current)

    a = Step.new(['word', nil, 1])
    p(a.reject {|x| x}.first)
    p(a.current)
    p(a.lazy.reject {|x| x}.first)
    p(a.current)