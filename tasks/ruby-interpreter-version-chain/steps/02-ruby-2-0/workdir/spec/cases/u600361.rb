# frozen_string_literal: false
def enum_test obj
    obj.map{|e|
      e
    }.sort
  end
def check_consistency_for_combinatorics(method)
    [ [], [:a, :b, :c, :d, :e] ].product([-2, 0, 2, 5, 6]) do |array, arg|
      assert_equal array.send(method, arg).to_a.size, array.send(method, arg).size,
        "inconsistent size for #{array}.#{method}(#{arg})"
    end
  end
@obj = Object.new
    class << @obj
      include Enumerable
      def foo(*a)
        a.each {|x| yield x }
      end
    end
    @sized = @obj.clone
    def @sized.size
      42
    end
begin; Enumerator.produce
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end

    # Without initial object
    passed_args = []
    enum = Enumerator.produce { |obj| passed_args << obj; (obj || 0).succ }
    p(enum.class)
    p(enum.size)
    p(enum.take(3))
    p(passed_args)

    # With initial object
    passed_args = []
    enum = Enumerator.produce(1) { |obj| passed_args << obj; obj.succ }
    p(enum.class)
    p(enum.size)
    p(enum.take(3))
    p(passed_args)

    # With initial keyword arguments
    passed_args = []
    enum = Enumerator.produce(a: 1, b: 1) { |obj| passed_args << obj; obj.shift if obj.respond_to?(:shift)}
    p(enum.class)
    p(enum.size)
    p(enum.take(4))
    p(passed_args)

    # Raising StopIteration
    words = "The quick brown fox jumps over the lazy dog.".scan(/\w+/)
    enum = Enumerator.produce { words.shift or raise StopIteration }
    p(enum.size)
    p(enum.class)
    p(enum.to_a)

    # Raising StopIteration
    object = [[[["abc", "def"], "ghi", "jkl"], "mno", "pqr"], "stuv", "wxyz"]
    enum = Enumerator.produce(object) { |obj|
      obj.respond_to?(:first) or raise StopIteration
      obj.first
    }
    p(enum.size)
    p(enum.class)
    begin; assert_equal [
        [[[["abc", "def"], "ghi", "jkl"], "mno", "pqr"], "stuv", "wxyz"],
        [[["abc", "def"], "ghi", "jkl"], "mno", "pqr"],
        [["abc", "def"], "ghi", "jkl"],
        ["abc", "def"],
        "abc",
      ], enum.to_a
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end