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
bug7696 = '[ruby-core:51470]'
    e=(1..42).lazy.take(2)
    p(e.next)
    p(e.next)
    e.rewind
    p(e.next)
    p(e.next)