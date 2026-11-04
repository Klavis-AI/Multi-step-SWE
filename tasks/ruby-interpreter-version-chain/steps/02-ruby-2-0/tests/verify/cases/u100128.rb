@obj = Object.new
    class << @obj
      include Enumerable
      def each
        yield 1
        yield 2
        yield 3
        yield 1
        yield 2
      end
    end
    @verbose = $VERBOSE
    $VERBOSE = nil
a = []
    @obj.each_with_index {|x, i| a << [x, i] }
    p(a)

    hash = Hash.new
    %w(cat dog wombat).each_with_index do |item, index|
      hash[item] = index
    end
    p(hash)