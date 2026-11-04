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
p(0.send(:zero?))
    assert_not_predicate(1, :zero?)