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
p([1, 2, 3].lazy.reject(&:even?).map(&:to_s).force)
    begin; [1, 2, 3].lazy.map(&:undefined).map(&:to_s).force
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end