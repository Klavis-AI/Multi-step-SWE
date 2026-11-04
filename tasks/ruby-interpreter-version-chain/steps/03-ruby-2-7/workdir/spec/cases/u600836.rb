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
bug7696 = '[ruby-core:51470]'
    e=(1..42).lazy.take(2)
    p(e.next)
    p(e.next)
    e.rewind
    p(e.next)
    p(e.next)