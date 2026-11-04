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
to_ary = Class.new {
      def initialize(value)
        @value = value
      end

      def to_ary
        [:to_ary, @value]
      end
    }
    p([1, 2, 3].flat_map {|x| to_ary.new(x)})
    p([1, 2, 3].lazy.flat_map {|x| to_ary.new(x)}.force)