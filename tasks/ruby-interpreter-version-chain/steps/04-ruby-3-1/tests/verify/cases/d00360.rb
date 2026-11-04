def f(**kw); kw; end
begin
  p(f(**{}))
rescue Exception => e
  p e.class
end
