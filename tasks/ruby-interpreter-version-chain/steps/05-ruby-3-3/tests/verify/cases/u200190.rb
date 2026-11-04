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
module M
    C = 1

    def self.m
      C
    end
  end
c = Class.new
    p(c.class.equal?(Class))
    p(c.superclass.equal?(Object))

    c = Class.new(Fixnum)
    p(c.class.equal?(Class))
    p(c.superclass.equal?(Fixnum))