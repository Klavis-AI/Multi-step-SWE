# frozen_string_literal: false
class C
    def call(*args, **kw)
      yield(self, *args, **kw)
    end
  end
def m_bug20570(*a, **nil)
    a
  end
p(m_bug20570(*[]))
    p(m_bug20570(*[1]))
    p(m_bug20570(*[1, 2]))
    h = nil
    p(m_bug20570(*[], **h))
    p(m_bug20570(*[1], **h))
    p(m_bug20570(*[1, 2], **h))

    p(m_bug20570(*[], **nil))
    p(m_bug20570(*[1], **nil))
    p(m_bug20570(*[1, 2], **nil))