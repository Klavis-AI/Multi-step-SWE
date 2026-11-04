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
a = Step.new(1..3)
    p(a.cycle(2).map(&:to_s).first)
    p(a.current)
    p(a.lazy.cycle(2).map(&:to_s).first)
    p(a.current)