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
p(@obj.none? {|x| x == 4 })
    p(!(@obj.none? {|x| x == 1 }))
    p(!(@obj.none? {|x| x == 3 }))
    p(%w{ant bear cat}.none? {|word| word.length == 5})
    p(!(%w{ant bear cat}.none? {|word| word.length >= 4}))
    p([].none?)
    p([nil].none?)
    p([nil,false].none?)