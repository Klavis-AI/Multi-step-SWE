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
p([1, 2, 3].flat_map {|x| x.to_s})
    p([1, 2, 3].lazy.flat_map {|x| x.to_s}.force)