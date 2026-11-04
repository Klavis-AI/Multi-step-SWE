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
    p(a.flat_map {|x| [x * 2]}.first)
    p(a.current)
    p(a.lazy.flat_map {|x| [x * 2]}.first)
    p(a.current)