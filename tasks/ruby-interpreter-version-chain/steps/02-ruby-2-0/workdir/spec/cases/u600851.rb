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
%i[select reject drop_while take_while map flat_map].each do |method|
      begin; [].lazy.send(method)
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end
    end