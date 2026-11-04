module M; def who; "M"; end; end
class C; include M; def who; "C"; end; end
p C.new.who
