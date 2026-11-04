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
a = Step.new(1..10)
    p(a.drop(5).first)
    p(a.current)
    p(a.lazy.drop(5).first)
    p(a.current)
    p(a.lazy.drop(5).to_a)