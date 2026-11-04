# frozen_string_literal: false
class C
    def call(*args, **kw)
      yield(self, *args, **kw)
    end
  end
def m_bug20570(*a, **nil)
    a
  end
mock = Class.new do
      def foo
        bar('x', y: 'z')
      end
      protected
      def bar(x, y)
        nil
      end
    end

    begin; mock.new.foo
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end