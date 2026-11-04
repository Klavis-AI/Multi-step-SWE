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
      @enum.each {|i| @current = i; yield i}
    end
  end
bug6428 = '[ruby-dev:45634]'
    a = Step.new(1..10)
    take5 = a.lazy.take(5)
    p(take5.force)
    p(take5.force)