# frozen_string_literal: false
class ClassOne
    attr :num_args
    @@subs = []
    def initialize(*args)
      @num_args = args.size
      @args = args
    end
    def [](n)
      @args[n]
    end
    def ClassOne.inherited(klass)
      @@subs.push klass
    end
    def subs
      @@subs
    end
  end
class ClassTwo < ClassOne
  end
class ClassThree < ClassOne
  end
class ClassFour < ClassThree
  end
class PrivateClass
  end
StrClone = String.clone
class CloneTest
    def foo; TEST; end
  end
CloneTest1 = CloneTest.clone
CloneTest2 = CloneTest.clone
class CloneTest1
    TEST = :C1
  end
class CloneTest2
    TEST = :C2
  end
a = ClassTwo.new
    p(a.class)
    p(a.num_args)

    a = ClassTwo.new(1, 2, 3)
    p(a.num_args)
    p(a[0])