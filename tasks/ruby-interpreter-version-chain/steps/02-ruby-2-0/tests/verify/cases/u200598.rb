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
a = Step.new(1..6)
    p(a.select {|x| x > 3}.first)
    p(a.current)
    p(a.lazy.select {|x| x > 3}.first)
    p(a.current)

    a = Step.new(['word', nil, 1])
    begin; a.select {|x| "x"+x}.first
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end
    p(a.current)
    p(a.lazy.select {|x| "x"+x}.first)
    p(a.current)