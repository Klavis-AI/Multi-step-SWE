# frozen_string_literal: false
class C
    def call(*args, **kw)
      yield(self, *args, **kw)
    end
  end
def m_bug20570(*a, **nil)
    a
  end
bug16603 = '[ruby-core:97047] [Bug #16603]'
    begin; p(**42)
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end
    begin; p(k:1, **42)
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end