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
e = Enumerator.new { |yielder|
      for i in 1..5
        yielder.yield(i, i.to_s)
      end
    }
    p(e.select {|x| x[0] % 2 == 0})
    p(e.lazy.select {|x| x[0] % 2 == 0}.force)