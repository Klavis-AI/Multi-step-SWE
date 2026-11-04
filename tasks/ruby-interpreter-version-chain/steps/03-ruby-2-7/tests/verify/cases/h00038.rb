module M; def who; "M+" + super; end; end
class C; def who; "C"; end; prepend M; end
p C.new.who
p C.ancestors.first(2).map(&:to_s)
