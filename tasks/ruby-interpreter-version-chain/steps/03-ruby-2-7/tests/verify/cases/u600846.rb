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
lazy = [1, 2, 3].lazy.map { |x| x * 2 }
    enum = lazy.eager
    p(enum.class)
    p(enum.size)
    p(enum.map { |x| x / 2 })