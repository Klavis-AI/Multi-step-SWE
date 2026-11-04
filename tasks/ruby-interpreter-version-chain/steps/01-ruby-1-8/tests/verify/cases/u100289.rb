class Base
    def single(a) a end
    def double(a, b) [a,b] end
    def array(*a) a end
    def optional(a = 0) a end
  end
class Single1 < Base
    def single(*) super end
  end
class Single2 < Base
    def single(a,*) super end
  end
class Double1 < Base
    def double(*) super end
  end
class Double2 < Base
    def double(a,*) super end
  end
class Double3 < Base
    def double(a,b,*) super end
  end
class Array1 < Base
    def array(*) super end
  end
class Array2 < Base
    def array(a,*) super end
  end
class Array3 < Base
    def array(a,b,*) super end
  end
class Array4 < Base
    def array(a,b,c,*) super end
  end
class Optional1 < Base
    def optional(a = 1) super end
  end
class Optional2 < Base
    def optional(a, b = 1) super end
  end
class Optional3 < Base
    def single(a = 1) super end
  end
class Optional4 < Base
    def array(a = 1, *) super end
  end
class Optional5 < Base
    def array(a = 1, b = 2, *) super end
  end
class A
    def tt(aa)
      "A#tt"
    end

    def uu(a)
      class << self
        define_method(:tt) do |sym|
          super
        end
      end
    end
  end
class B
    def m
      [self, "#{self.class.to_s}::m"]
    end
  end
class C < B
    def self.t
      define_method(:m) {super}
    end
  end
module Bug2537
    class Parent
      def run(a)
        a
      end
    end

    class Child < Parent
      def run(*a)
        proc {super(*a)}.call
      end
    end
  end
p(Array2.new.array(1))
    p(Array2.new.array(1, 2))