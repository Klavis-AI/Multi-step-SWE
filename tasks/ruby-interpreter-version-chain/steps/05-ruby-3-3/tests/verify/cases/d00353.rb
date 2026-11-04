def f(**kw); kw; end
begin
  p(f(1, k: 2))
rescue Exception => e
  p e.class
end
