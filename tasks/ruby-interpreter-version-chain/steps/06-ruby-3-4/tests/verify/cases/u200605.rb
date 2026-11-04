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
p([?a, ?b, ?c].flat_map {|x| {x=>x.ord}})
    p([?a, ?b, ?c].lazy.flat_map {|x| {x=>x.ord}}.force)