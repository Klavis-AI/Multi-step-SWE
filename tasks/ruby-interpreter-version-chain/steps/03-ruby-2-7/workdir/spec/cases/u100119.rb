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
p(@obj.one? {|x| x == 3 })
    p(!(@obj.one? {|x| x == 1 }))
    p(!(@obj.one? {|x| x == 4 }))
    p(%w{ant bear cat}.one? {|word| word.length == 4})
    p(!(%w{ant bear cat}.one? {|word| word.length > 4}))
    p(!(%w{ant bear cat}.one? {|word| word.length < 4}))
    p(!([ nil, true, 99 ].one?))
    p([ nil, true, false ].one?)