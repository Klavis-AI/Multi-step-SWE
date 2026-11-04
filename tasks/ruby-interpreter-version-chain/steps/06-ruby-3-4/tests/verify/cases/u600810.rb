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
p([[1,2]].cycle.lazy.take(3).flat_map {|x| x}.to_a)