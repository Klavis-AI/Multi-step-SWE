# frozen_string_literal: false
class DummyNumeric < Numeric
    def to_int
      1
    end
  end
def assert_eql(a, b, mess)
    assert a.eql?(b), "expected #{a} & #{b} to be eql? #{mess}"
  end
@verbose = $VERBOSE
p(-1.send(:negative?))
    assert_not_predicate(0, :negative?)
    assert_not_predicate(1, :negative?)